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

### Test mongodb queries

Get inside app-db container

```bash
docker compose exec audit-db sh -c 'mongosh \
  --username "$MONGO_USER" \
  --password "$MONGO_PASSWORD" \
  --authenticationDatabase "$MONGO_DB_AUDIT" \
  "$MONGO_DB_AUDIT"'
```

Or create a `queries.js` at root project

```bash
touch queries.js
```

Execute command inside container with

```bash
docker compose exec audit-db sh -c 'mongosh \
  --username "$MONGO_USER" \
  --password "$MONGO_PASSWORD" \
  --authenticationDatabase "$MONGO_DB_AUDIT" \
  "$MONGO_DB_AUDIT" \
  /queries.js'
```

## Change end of line from windows to linux

```bash
dos2unix <fileName>
```

## How to build client

### client on Windows

First, check if you already have the required tools:

```bash
qmake6 --version && cmake --version && openssl --version
```

If anything is missing, you have two options:

#### Install Qt 6.4.2+ with MinGW

**Option A — via WSL (Windows Subsystem for Linux):**

```bash
sudo apt install qt6-base-dev qt6-base-dev-tools
sudo apt install cmake
sudo apt install qt6-websockets-dev
```

**Option B — via the Qt official installer:**

Download Qt from [https://www.qt.io/download-open-source](https://www.qt.io/download-open-source) (free community version, account required).
Install `Qt` with `gcc`, `g++` and `cmake` to avoid path issues.
Qt **6.4.2 minimum**, **6.10.2** was used for development.

**During installation, select:**

- Qt 6.10.2 (or 6.4.2+)
- **MinGW 13.1 64-bit** compiler

Then add these to your `PATH`:

```
C:\Qt\Tools\QtCreator\bin
C:\Qt\6.x.x\mingw_64\bin
C:\Qt\Tools\CMake_64\bin
```

Verify installation:

```powershell
where gcc
# Should show: C:\Qt\Tools\mingw1310_64\bin\gcc.exe

where cmake
# Should show: C:\Qt\Tools\CMake_64\bin\cmake.exe

```

#### Install OpenSSL compatible with Qt MinGW

Install **MSYS2** (a separate tool, independent from Qt) from: <https://www.msys2.org/>
Open **MSYS2 MinGW 64-bit terminal** (NOT UCRT64, NOT CLANG64):

```bash
pacman -S mingw-w64-x86_64-openssl
```

Verify OpenSSL libraries exist:

```bash
ls -la /mingw64/lib/libssl.dll.a
ls -la /mingw64/lib/libcrypto.dll.a
ls -la /mingw64/include/openssl/ssl.h
```

**Build the client:**

From **PowerShell**:

```powershell
./client/windows-build.bat
```

From **Git Bash**:

#### Release

```bash
powershell.exe -NoProfile -Command "& '$(cygpath -w ./client/windows-build.bat)'"
```

**Run the client:**

```bash
./client/build/client.exe
```

> **WSL:** if you get EGL/MESA errors, add `export LIBGL_ALWAYS_SOFTWARE=1` to your `~/.bashrc`

---

### client on Linux

Check your tools first:

```bash
qmake6 --version && cmake --version
```

Install if needed:

```bash
sudo apt install qt6-base-dev qt6-base-dev-tools
sudo apt install cmake
sudo apt install qt6-websockets-dev
sudo apt install libssl-dev
```

Make the scripts executable (only needed once):

```bash
chmod +x ./client/linux-build.sh
chmod +x ./client/run.sh
```

Build then run:

```bash
./client/linux-build.sh
./client/run.sh
```

**WSL only:** if you get EGL/MESA rendering errors, add this to your `~/.bashrc` and restart your terminal:

```bash
export LIBGL_ALWAYS_SOFTWARE=1
```

> WSL has no GPU access, so Qt's hardware OpenGL rendering fails. This flag forces software rendering instead.
