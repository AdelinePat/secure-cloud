db = db.getSiblingDB(process.env.MONGO_DB_AUDIT);

db.createCollection("auditLogs");
db.createCollection("systemLogs");

db.auditLogs.createIndex({ timestamp: 1 });

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