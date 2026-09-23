#pragma once
class Database;
// Creates or migrates the schema (SPEC §4). Returns the schema version now in the file, 0 on failure.
int ensureSchema(Database &db);
constexpr int kSchemaVersion = 7;
