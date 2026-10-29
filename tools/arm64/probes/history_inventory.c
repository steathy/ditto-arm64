/* Read-only history checks. Never emit clipboard contents. */
#include <stdio.h>
#include <string.h>
#include "sqlite3mc_amalgamation.h"

static int compare_history(sqlite3 *current, const char *baseline_path) {
    sqlite3 *baseline = 0;
    const char *tables[] = {"Main", "Data", "Types"};
    int i, ok = 1;
    if (sqlite3_open_v2(baseline_path, &baseline, SQLITE_OPEN_READONLY, 0) != SQLITE_OK) {
        sqlite3_close(baseline);
        return 0;
    }
    for (i = 0; i < 3; ++i) {
        sqlite3_stmt *before = 0, *after = 0;
        char sql[80];
        int old_status, new_status, missing = 0, changed = 0;
        sprintf(sql, "SELECT * FROM %s ORDER BY lID", tables[i]);
        if (sqlite3_prepare_v2(baseline, sql, -1, &before, 0) != SQLITE_OK ||
            sqlite3_prepare_v2(current, sql, -1, &after, 0) != SQLITE_OK) {
            ok = 0;
        } else {
            new_status = sqlite3_step(after);
            while ((old_status = sqlite3_step(before)) == SQLITE_ROW) {
                sqlite3_int64 id = sqlite3_column_int64(before, 0);
                int column, different = 0;
                while (new_status == SQLITE_ROW && sqlite3_column_int64(after, 0) < id)
                    new_status = sqlite3_step(after);
                if (new_status != SQLITE_ROW || sqlite3_column_int64(after, 0) != id) {
                    ++missing;
                    continue;
                }
                if (sqlite3_column_count(before) != sqlite3_column_count(after)) different = 1;
                for (column = 0; !different && column < sqlite3_column_count(before); ++column) {
                    int old_type = sqlite3_column_type(before, column);
                    int new_type = sqlite3_column_type(after, column);
                    const void *old_data = sqlite3_column_blob(before, column);
                    const void *new_data = sqlite3_column_blob(after, column);
                    int length = sqlite3_column_bytes(before, column);
                    if (old_type != new_type || length != sqlite3_column_bytes(after, column) ||
                        (length && memcmp(old_data, new_data, length) != 0)) different = 1;
                }
                changed += different;
            }
            if (old_status != SQLITE_DONE || (new_status != SQLITE_ROW && new_status != SQLITE_DONE)) ok = 0;
            if (missing || changed) ok = 0;
            printf("%s_missingBaselineRows=%d\n%s_changedBaselineRows=%d\n", tables[i], missing, tables[i], changed);
        }
        sqlite3_finalize(before);
        sqlite3_finalize(after);
    }
    sqlite3_close(baseline);
    return ok;
}

int main(int argc, char **argv) {
    sqlite3 *db = 0;
    sqlite3_stmt *stmt = 0;
    const char *sql[] = {"PRAGMA quick_check", "SELECT count(*) FROM Main",
        "SELECT count(*) FROM Data", "SELECT count(*) FROM Types",
        "SELECT count(*) FROM Main WHERE bIsGroup <> 0"};
    const char *labels[] = {"integrity", "clipsAndGroups", "formats", "types", "groups"};
    int i, ok = 1;
    if (argc != 2 && argc != 3) return 10;
    if (sqlite3_open_v2(argv[1], &db, SQLITE_OPEN_READONLY, 0) != SQLITE_OK) return 11;
    sqlite3_busy_timeout(db, 5000);
    for (i = 0; i < 5; ++i) {
        if (sqlite3_prepare_v2(db, sql[i], -1, &stmt, 0) != SQLITE_OK ||
            sqlite3_step(stmt) != SQLITE_ROW) {
            fprintf(stderr, "History metadata check failed: %s\n", labels[i]);
            ok = 0;
        } else {
            printf("%s=%s\n", labels[i], sqlite3_column_text(stmt, 0));
            if (i == 0 && sqlite3_stricmp((const char *)sqlite3_column_text(stmt, 0), "ok") != 0) ok = 0;
        }
        sqlite3_finalize(stmt);
        stmt = 0;
    }
    if (argc == 3) ok = compare_history(db, argv[2]) && ok;
    sqlite3_close(db);
    return ok ? 0 : 12;
}
