#include <iostream>
#include <sqlite3.h>

int main() {
    sqlite3* db = nullptr;

    int result = sqlite3_open(
        "database/civicfix.db",
        &db
    );

    if (result == SQLITE_OK) {
        std::cout << "SQLite connected successfully!\n";
        std::cout << "Database: CivicFix Pakistan\n";
    }
    else {
        std::cout << "Database connection failed!\n";
        std::cout << sqlite3_errmsg(db) << "\n";
    }

    sqlite3_close(db);

    return 0;
}