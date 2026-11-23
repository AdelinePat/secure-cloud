db = db.getSiblingDB(process.env.MONGO_DB_AUDIT);

db.createCollection("auditLogs");
db.createCollection("systemLogs");

// Append-only, tamper-evident chain. The unique indexes are not just
// performance hints: they are what serialises concurrent appends without a
// multi-document transaction (standalone mongod has no replica-set txns).
// Names must match MongoAuditRepository::ensureIndexes(), which re-creates
// them on every boot (same name + same keys = no-op).
// Note: mongosh's createIndexes() takes key patterns only, not full specs,
// so each index is created with createIndex(keys, options).
db.auditLogs.createIndex({ seq: 1 }, { name: "uniq_seq", unique: true });
db.auditLogs.createIndex({ prev_hash: 1 }, { name: "uniq_prev_hash", unique: true });
db.auditLogs.createIndex({ hash: 1 }, { name: "uniq_hash", unique: true });
db.auditLogs.createIndex({ event_type: 1, occurred_at: -1 }, { name: "idx_event_type_occurred_at" });
db.auditLogs.createIndex({ service_name: 1, occurred_at: -1 }, { name: "idx_service_occurred_at" });
db.auditLogs.createIndex({ actor_id: 1 }, { name: "idx_actor_id" });
db.auditLogs.createIndex({ target_id: 1 }, { name: "idx_target_id" });

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
