#include <SWI-Prolog.h>
#include <SWI-Stream.h>
#include <libpq-fe.h>
#include <stdbool.h>
#include <string.h>

/*
 * connect(+ConnInfo, -ConnectionHandler)
 * Connects to a PostgreSQL database
 * ConnInfo is a string like "host=localhost dbname=mydbname user=postgres password=myprettypassword"
 */
static foreign_t pl_pg_connect(term_t conn_info, term_t conn_handler){
  char *conn_string;
  if (!PL_get_chars(conn_info, &conn_string, CVT_ALL|REP_UTF8))
    return PL_type_error("atom", conn_info);

  PGconn *conn = PQconnectdb(conn_string);

  if (PQstatus(conn) != CONNECTION_OK) {
    char *error_msg = PQerrorMessage(conn);
    PQfinish(conn);
    conn = NULL;
    return PL_domain_error("pg_connection", conn_info);
  }

  if (!PL_unify_pointer(conn_handler, conn)) {
    return PL_warning("pl_pg_connect: failed to unify connection handler");
  }
  return TRUE;
}

/*
 * pg_disconnect
 * Closes the current PostgreSQL connection
 */
static foreign_t pl_pg_disconnect(term_t connection_handler) {
  PGconn *conn;
  if (!PL_get_pointer_ex(connection_handler, (void**)&conn)) {
    return PL_warning("pl_pg_disconnect: failed to get connection handler");
  }
  if (conn != NULL) {
    PQfinish(conn);
  }
  return TRUE;
}

/*
 * pg_query(+Query, -Result)
 * Executes a query and returns the result as a list of rows
 * Each row is represented as a list of values
 */
static foreign_t pl_pg_query(term_t query_term, term_t connection_handler, term_t result_term){
  char *query;
  PGresult *res;
  PGconn *conn;
  if (!PL_get_pointer_ex(connection_handler, (void**)&conn)) {
    return PL_warning("pl_pg_query: failed to get connection handler");
  }

  if (!PL_get_chars(query_term, &query, CVT_ALL|REP_UTF8))
    return PL_type_error("atom", query_term);

  if (conn == NULL)
    return PL_existence_error("pg_connection", query_term);

  /* Execute query */
  res = PQexec(conn, query);

  if (PQresultStatus(res) != PGRES_TUPLES_OK &&
      PQresultStatus(res) != PGRES_COMMAND_OK) {
    char *error_msg = PQerrorMessage(conn);
    PQclear(res);
    return PL_domain_error("pg_query", query_term);
  }

  /* Build result list */
  int nrows = PQntuples(res);
  int ncols = PQnfields(res);

  term_t row_list = PL_new_term_ref();
  PL_put_nil(row_list);

  /* Build rows in reverse order */
  for (int i = nrows - 1; i >= 0; i--) {
    term_t row = PL_new_term_ref();
    PL_put_nil(row);

    /* Build columns in reverse order */
    for (int j = ncols - 1; j >= 0; j--) {
      term_t cell = PL_new_term_ref();
      term_t new_row = PL_new_term_ref();

      if (PQgetisnull(res, i, j)) {
	PL_put_atom_chars(cell, "null");
      } else {
	char *value = PQgetvalue(res, i, j);
	PL_put_atom_chars(cell, value);
      }

      PL_cons_list(new_row, cell, row);
      row = new_row;
    }

    /* Add row to result list */
    term_t new_list = PL_new_term_ref();
    PL_cons_list(new_list, row, row_list);
    row_list = new_list;
  }

  PQclear(res);

  return PL_unify(result_term, row_list);
}

install_t install_prologlib(void) {
  PL_register_foreign("pg_connect", 2, pl_pg_connect, 0);
  PL_register_foreign("pg_disconnect", 1, pl_pg_disconnect, 0);
  PL_register_foreign("pg_query", 3, pl_pg_query, 0);
}
