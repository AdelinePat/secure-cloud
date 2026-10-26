db = db.getSiblingDB(process.env.MONGO_DB_AUDIT);

db.createCollection("auditLogs");
db.createCollection("systemLogs");

// Append-only, tamper-evident chain. The unique indexes are not just
// performance hints: they are what serialises concurrent appends without a
// multi-document transaction (standalone mongod has no replica-set txns).
db.auditLogs.createIndexes([
  { key: { seq: 1 }, name: "uniq_seq", unique: true },
  { key: { prev_hash: 1 }, name: "uniq_prev_hash", unique: true },
  { key: { hash: 1 }, name: "uniq_hash", unique: true },
  { key: { event_type: 1, occurred_at: -1 }, name: "idx_event_type_occurred_at" },
  { key: { service_name: 1, occurred_at: -1 }, name: "idx_service_occurred_at" },
  { key: { actor_id: 1 }, name: "idx_actor_id" },
  { key: { target_id: 1 }, name: "idx_target_id" }
]);

db.systemLogs.createIndex({ timestamp: -1 });

db.createUser({
    user: process.env.MONGO_USER,
    pwd: process.env.MONGO_PASSWORD,
    roles: [
        {
            role: "readWrite",
            db: process.env.MONGO_DB_AUDIT
        }
    ]
});


print("Audit database initialized successfully.");
