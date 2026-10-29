#include <stdio.h>
#include <string.h>
#include "sqlite3mc_amalgamation.h"

static int expect(sqlite3 *db, const char *sql, const char *expected) {
    sqlite3_stmt *statement = 0;
    int ok = sqlite3_prepare_v2(db, sql, -1, &statement, 0) == SQLITE_OK;
    if (ok) ok = sqlite3_step(statement) == SQLITE_ROW;
    if (ok) {
        const unsigned char *value = sqlite3_column_text(statement, 0);
        ok = value && strcmp((const char *)value, expected) == 0;
    }
    if (!ok) fprintf(stderr, "FAIL: %s; %s\n", sql, sqlite3_errmsg(db));
    sqlite3_finalize(statement);
    return ok;
}

int main(int argc, char **argv) {
    sqlite3 *db = 0;
    char *error = 0;
    int ok;
    if (argc != 3) return 10;
    if (sqlite3_open_v2(argv[1], &db, SQLITE_OPEN_READONLY, 0) != SQLITE_OK) return 11;
    ok = expect(db, "PRAGMA quick_check", "ok");
    ok = expect(db, "SELECT count(*) FROM Main", "0") && ok;
    sqlite3_enable_load_extension(db, 1);
    if (sqlite3_load_extension(db, argv[2], "sqlite3_icu_init", &error) != SQLITE_OK) {
        fprintf(stderr, "ICU extension load failed: %s\n", error ? error : sqlite3_errmsg(db));
        sqlite3_free(error);
        sqlite3_close(db);
        return 12;
    }
    ok = expect(db, "SELECT regexp('^Ditto.*', 'Ditto ARM64')", "1") && ok;
    ok = expect(db, "SELECT upper('Stra\xc3\x9f" "e')", "STRASSE") && ok;
    sqlite3_close(db);
    if (!ok) return 13;
    puts("PASS: Ditto-created database integrity, zero captured clips, native ICU extension, regex and Unicode uppercase");
    return 0;
}
