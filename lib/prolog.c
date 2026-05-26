#include <SWI-Prolog.h>
#include <SWI-Stream.h>
#include <libpq-fe.h>
#include <stdlib.h>

typedef struct pg_connection {
  PGconn *conn;
} pg_connection;

static int release_connection(atom_t atom);
static int write_connection(IOSTREAM *stream, atom_t atom, int flags);

static PL_blob_t connection_blob = {
  PL_BLOB_MAGIC,
  PL_BLOB_UNIQUE,
  "pg_connection",
  release_connection,
  NULL,
  write_connection,
  NULL,
  NULL,
  NULL,
  0
};

static int close_connection(pg_connection *connection) {
  if (connection == NULL) {
    return TRUE;
  }

  if (connection->conn != NULL) {
    PQfinish(connection->conn);
    connection->conn = NULL;
  }

  return TRUE;
}

static int release_connection(atom_t atom) {
  pg_connection **slot = PL_blob_data(atom, NULL, NULL);
  pg_connection *connection = *slot;

  close_connection(connection);
  free(connection);
  return TRUE;
}

static int write_connection(IOSTREAM *stream, atom_t atom, int flags) {
  pg_connection **slot = PL_blob_data(atom, NULL, NULL);
  pg_connection *connection = *slot;

  (void)flags;
  Sfprintf(stream,
           "<pg_connection>(%s)",
           connection == NULL || connection->conn == NULL ? "closed" : "open");
  return TRUE;
}

static foreign_t pg_error(const char *operation, const char *message) {
  term_t exception = PL_new_term_ref();

  if (message == NULL || message[0] == '\0') {
    message = "unknown PostgreSQL error";
  }

  if (!PL_unify_term(exception,
                     PL_FUNCTOR_CHARS, "error", 2,
                       PL_FUNCTOR_CHARS, "postgres_error", 2,
                         PL_CHARS, operation,
                         PL_CHARS, message,
                       PL_VARIABLE)) {
    return FALSE;
  }

  return PL_raise_exception(exception);
}

static int get_text(term_t term, char **value) {
  return PL_get_chars(term, value, CVT_ATOM | CVT_STRING | REP_UTF8);
}

static foreign_t get_connection(term_t connection_handler, PGconn **conn) {
  pg_connection **slot;
  pg_connection *connection;
  size_t len;
  PL_blob_t *type;

  if (!PL_get_blob(connection_handler, (void **)&slot, &len, &type) ||
      type != &connection_blob ||
      len != sizeof(*slot)) {
    return PL_type_error("pg_connection", connection_handler);
  }

  connection = *slot;

  if (connection == NULL || connection->conn == NULL) {
    return PL_existence_error("pg_connection", connection_handler);
  }

  *conn = connection->conn;
  return TRUE;
}

static foreign_t get_connection_blob(term_t connection_handler,
                                     pg_connection **connection) {
  pg_connection **slot;
  size_t len;
  PL_blob_t *type;

  if (!PL_get_blob(connection_handler, (void **)&slot, &len, &type) ||
      type != &connection_blob ||
      len != sizeof(*slot)) {
    return PL_type_error("pg_connection", connection_handler);
  }

  *connection = *slot;
  return TRUE;
}

static int put_pg_value(term_t cell, PGresult *result, int row, int col) {
  if (PQgetisnull(result, row, col)) {
    return PL_put_atom_chars(cell, "null");
  }

  return PL_put_atom_chars(cell, PQgetvalue(result, row, col));
}

static int unify_pg_result(term_t result_term, PGresult *result) {
  int nrows = PQntuples(result);
  int ncols = PQnfields(result);

  term_t rows = PL_new_term_ref();
  term_t row_values = PL_new_term_ref();
  term_t cell = PL_new_term_ref();
  term_t new_row = PL_new_term_ref();
  term_t new_rows = PL_new_term_ref();

  PL_put_nil(rows);

  for (int row = nrows - 1; row >= 0; row--) {
    PL_put_nil(row_values);

    for (int col = ncols - 1; col >= 0; col--) {
      if (!put_pg_value(cell, result, row, col)) {
        return FALSE;
      }

      if (!PL_cons_list(new_row, cell, row_values)) {
        return FALSE;
      }

      if (!PL_put_term(row_values, new_row)) {
        return FALSE;
      }
    }

    if (!PL_cons_list(new_rows, row_values, rows)) {
      return FALSE;
    }

    if (!PL_put_term(rows, new_rows)) {
      return FALSE;
    }
  }

  return PL_unify(result_term, rows);
}

static foreign_t pl_pg_connect(term_t conn_info, term_t conn_handler) {
  char *conn_string;
  if (!get_text(conn_info, &conn_string)) {
    return PL_type_error("text", conn_info);
  }

  PGconn *conn = PQconnectdb(conn_string);

  if (conn == NULL) {
    return pg_error("connect", "PQconnectdb returned NULL");
  }

  if (PQstatus(conn) != CONNECTION_OK) {
    term_t exception = PL_new_term_ref();
    const char *message = PQerrorMessage(conn);

    if (!PL_unify_term(exception,
                       PL_FUNCTOR_CHARS, "error", 2,
                         PL_FUNCTOR_CHARS, "postgres_error", 2,
                           PL_CHARS, "connect",
                           PL_CHARS, message,
                         PL_VARIABLE)) {
      PQfinish(conn);
      return FALSE;
    }

    PQfinish(conn);
    return PL_raise_exception(exception);
  }

  pg_connection *connection = malloc(sizeof(*connection));
  if (connection == NULL) {
    PQfinish(conn);
    return PL_resource_error("memory");
  }

  connection->conn = conn;

  if (!PL_unify_blob(conn_handler,
                     &connection,
                     sizeof(connection),
                     &connection_blob)) {
    close_connection(connection);
    free(connection);
    return FALSE;
  }

  return TRUE;
}

static foreign_t pl_pg_disconnect(term_t connection_handler) {
  pg_connection *connection;
  if (!get_connection_blob(connection_handler, &connection)) {
    return FALSE;
  }

  close_connection(connection);
  return TRUE;
}

static foreign_t pl_pg_query(term_t query_term, term_t connection_handler, term_t result_term) {
  char *query;
  PGconn *conn;

  if (!get_text(query_term, &query)) {
    return PL_type_error("text", query_term);
  }

  if (!get_connection(connection_handler, &conn)) {
    return FALSE;
  }

  PGresult *result = PQexec(conn, query);
  if (result == NULL) {
    return pg_error("query", PQerrorMessage(conn));
  }

  ExecStatusType status = PQresultStatus(result);
  if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
    term_t exception = PL_new_term_ref();
    const char *message = PQresultErrorMessage(result);

    if (message == NULL || message[0] == '\0') {
      message = PQerrorMessage(conn);
    }

    if (!PL_unify_term(exception,
                       PL_FUNCTOR_CHARS, "error", 2,
                         PL_FUNCTOR_CHARS, "postgres_error", 2,
                           PL_CHARS, "query",
                           PL_CHARS, message,
                         PL_VARIABLE)) {
      PQclear(result);
      return FALSE;
    }

    PQclear(result);
    return PL_raise_exception(exception);
  }

  int rc = unify_pg_result(result_term, result);
  PQclear(result);

  return rc;
}

install_t install_prologlib(void) {
  PL_register_blob_type(&connection_blob);
  PL_register_foreign("pg_connect", 2, pl_pg_connect, 0);
  PL_register_foreign("pg_disconnect", 1, pl_pg_disconnect, 0);
  PL_register_foreign("pg_query", 3, pl_pg_query, 0);
}
