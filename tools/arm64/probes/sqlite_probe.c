#include <stdio.h>
#include <string.h>
#include "sqlite3mc_amalgamation.h"

int main(int argc, char **argv) {
    sqlite3 *db = 0;
    sqlite3_stmt *stmt = 0;
    const char *key = "ditto-arm64-audit-only";
    int rc;
    if (argc != 2) return 10;
    printf("SQLite %s; %s\n", sqlite3_libversion(), sqlite3mc_version());
    rc = sqlite3_open(argv[1], &db);
    if (rc != SQLITE_OK) return 11;
    if (sqlite3_key(db, key, (int)strlen(key)) != SQLITE_OK) return 12;
    rc = sqlite3_exec(db, "CREATE TABLE probe(value TEXT); INSERT INTO probe VALUES('ARM64');", 0, 0, 0);
    if (rc != SQLITE_OK) { fprintf(stderr, "%s\n", sqlite3_errmsg(db)); return 13; }
    sqlite3_close(db);
    if (sqlite3_open(argv[1], &db) != SQLITE_OK) return 14;
    if (sqlite3_key(db, key, (int)strlen(key)) != SQLITE_OK) return 15;
    if (sqlite3_prepare_v2(db, "SELECT value FROM probe", -1, &stmt, 0) != SQLITE_OK) return 16;
    if (sqlite3_step(stmt) != SQLITE_ROW || strcmp((const char *)sqlite3_column_text(stmt, 0), "ARM64")) return 17;
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    if (sqlite3_open(argv[1], &db) != SQLITE_OK) return 18;
    sqlite3_key(db, "wrong-key", 9);
    rc = sqlite3_exec(db, "SELECT * FROM probe", 0, 0, 0);
    sqlite3_close(db);
    if (rc == SQLITE_OK) return 19;
    puts("PASS: encrypted database creation, correct-key reopen, and wrong-key rejection");
    return 0;
}
