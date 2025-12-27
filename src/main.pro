:- use_foreign_library('./result/lib/prologlib.so').

example_basic :-
  pg_connect("host=localhost dbname=prolog_store user=admin password=admin", Conn),
  pg_query("SELECT 1", Conn, Result),
  write('Result: '), writeln(Result),
  pg_disconnect(Conn).

