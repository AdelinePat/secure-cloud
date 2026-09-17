# secure-cloud

## Prerequistes
- Docker / Docker desktop installed and running

## Quick start
Start all services
```bash
docker compose up
```
stop all services
```bash
docker compose down
```

stop all services and delete volumes
```bash
docker compose down -v
```

Start a service
```bash
docker compose start <service> 
```

Stop a service
```bash
docker compose stop <service> 
```

Get logs from a service
```bash
docker compose logs <service> 
```

### Test PostgreSQL queries
Get inside app-db container

```bash
docker compose exec app-db sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB"'
```
Or create a `queries.sql` at root project
```bash
touch queries.sql
```
Execute command inside container with
```bash
docker compose exec app-db sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -f queries.sql'
```

## Change end of line from windows to linux
```
dos2unix <fileName>
```