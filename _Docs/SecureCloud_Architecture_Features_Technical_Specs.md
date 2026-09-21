# SecureCloud — Architecture, Features & Technical Specifications

> Extracted and consolidated from the provided SecureCloud project documents.
> This document focuses only on information relevant to architecture, implementation, features, constraints, and acceptance criteria.

---

## 1. Project Scope

SecureCloud is a professional secure communications platform intended for sensitive organizations and users, including:

- Humanitarian organizations operating in hostile environments
- Investigative journalists
- Sensitive enterprises (finance, healthcare, defense)
- Public administrations requiring digital sovereignty and compliance

### Core product objectives

- End-to-end encrypted instant messaging
- Secure file sharing with complete auditing
- Self-hosted and scalable infrastructure
- Automated deployment of security updates
- Professional, cross-platform native user experience

### Key security principle

**The server must never access the plaintext content of communications.** It acts as a router for encrypted data.

---

## 2. Target Architecture

### 2.1 Architectural style

The project specifies a **C++ microservices architecture** with a central API Gateway.

```text
                         ┌─────────────────────────┐
                         │       Qt Client         │
                         │ Windows / Linux / macOS │
                         └────────────┬────────────┘
                                      │
                         ┌────────────▼────────────┐
                         │       API Gateway       │
                         │ JWT validation + routing│
                         └──────┬─────┬─────┬──────┘
                                │     │     │
              ┌─────────────────┘     │     └─────────────────┐
              ▼                       ▼                       ▼
     ┌────────────────┐      ┌────────────────┐      ┌────────────────┐
     │ Messaging      │      │ Auth Service   │      │ Files Service  │
     │ Service        │      │                │      │                │
     └────────────────┘      └────────────────┘      └────────────────┘
              │                       │                       │
              └──────────────┬────────┴───────────┬──────────┘
                             ▼                    ▼
                    ┌────────────────┐   ┌────────────────┐
                    │ Audit Service  │   │ Deploy Service │
                    └────────────────┘   └────────────────┘
                             │
                    ┌────────▼────────┐
                    │   MongoDB       │
                    └─────────────────┘
```

### 2.2 Main services

The deployment architecture includes:

1. **auth-service**
   - Authentication
   - Identity management
   - Authorization and permissions
   - Token validation and revocation

2. **messaging-service**
   - Real-time messaging
   - 1-to-1 conversations
   - Group conversations
   - Presence and notifications
   - Conversation threading

3. **files-service**
   - Secure file upload/download
   - Streaming encryption
   - Chunking
   - Compression
   - Encrypted virtual filesystem
   - Block-level deduplication

4. **audit-service**
   - Encrypted audit logs
   - Compliance-related events
   - Tracking of actions such as file uploads

5. **deploy-service**
   - Infrastructure/deployment management

6. **postgresql**
   - Shared database component defined in the orchestration requirements

### 2.3 API Gateway

Responsibilities explicitly specified:

- JWT validation
- Request routing

Implementation must also account for the identified failure points:

- Routing failures
- Rate-limiting bugs

---

## 3. Inter-Service Communication

### Example interaction: file sent through chat

```text
1. Qt Interface
      ↓
2. Messaging Service
      ↓
3. Auth Service
   Verify permissions
      ↓
4. Files Service
   Upload/process file
      ↓
5. Audit Service
   Record "file_uploaded"
      ↓
6. Qt Interface
   Notify administrator when applicable
```

---

## 4. Client Application

### Technology

**Qt** is the required UI technology.

### Supported platforms

- Windows
- Linux
- macOS

### Operational requirements

The client must remain usable under degraded field conditions:

- Weak satellite connection: **256 kbps**
- High ambient temperature: **45°C**
- Dusty environments
- Aging hardware

### Performance targets

- Startup time: **less than 2 seconds**
- RAM consumption: **less than 50 MB**
- Responsive interface under degraded network conditions

### Implementation considerations

The UI should cover the functionality currently spread across multiple tools:

- Secure messaging
- File sharing
- Notifications
- Administrative interactions where required

---

## 5. Functional Features

### 5.1 Secure Messaging

#### Conversations

- One-to-one conversations
- Group conversations with up to **100 participants**
- Conversation threads for organization

#### Security

- End-to-end encryption
- AES-256 specified as the encryption algorithm
- Secure presence information

#### Real-time communication

- Secure WebSockets
- Presence status
- Intelligent notifications
- Optimized threading model for high load

#### Local data handling

- Search through conversation history
- Encrypted local indexing

#### Performance target

- Peak capacity: **10,000 messages per second**
- Response time target: **less than 100 ms**

#### Reliability

The architecture identifies:

- Message loss
- Authentication failures
- Service crashes

as critical failure conditions to prevent and test.

---

### 5.2 Secure File Sharing

#### Required capabilities

- Native HTTP/HTTPS server
- Streaming encryption
- Automatic chunking
- Integrated zlib compression
- Encrypted virtual filesystem
- Block-level deduplication

#### Performance target

- Upload **500 MB in less than 3 minutes**

#### Field requirements

The system must support scenarios including:

- Emergency scanner uploads
- Medical data transfers
- Backup operations without internet connectivity

#### Data integrity risks to address

- File corruption
- Transfer interruption
- Service crashes during file operations

---

### 5.3 Authentication & Authorization

#### Main capabilities

- User and identity management
- Multi-factor authentication
- Granular permissions
- Token lifecycle management
- Immediate access revocation
- REST API for service integration

#### Required REST endpoints

##### `POST /auth/login`

Initial authentication with multi-factor verification.

##### `POST /auth/refresh`

JWT token renewal with continuous validation.

##### `GET /auth/validate`

Fast token validation with optimized caching.

##### `POST /auth/permissions`

Permission verification for a specific action.

##### `POST /auth/revoke`

Immediate revocation of all user tokens.

#### Scale requirement

The service is expected to centrally manage access for approximately **45,000 users**.

#### Critical scenarios

- Login over unstable satellite connectivity
- Immediate access revocation during emergency evacuation

---

### 5.4 Audit and Compliance

The specification requires:

- Complete auditability
- Logging of all relevant actions
- Encrypted audit logs

Example event:

```text
file_uploaded
```

The implementation must prevent:

- Log loss
- Audit service failures

---

## 6. Security Requirements

### 6.1 Encryption

Specified requirements include:

- End-to-end encryption
- AES-256
- Encrypted communications
- Encrypted audit logs
- Streaming encryption for files

### 6.2 Cryptographic libraries

The documents specify the use of established cryptographic libraries, including:

- OpenSSL
- libsodium

#### Implementation principle

**Do not implement custom cryptography unless explicitly required by the project.**

The source documents emphasize using proven and audited cryptographic libraries.

### 6.3 Server visibility

The architecture requirement is:

> The server must not see the content of communications.

This implies that encryption/decryption responsibilities and key management must be designed so that backend routing services do not require access to plaintext message content.

### 6.4 Authentication security

Required protections include:

- Multi-factor authentication
- JWT validation
- Token revocation
- Granular authorization
- Resistance to brute-force attacks

### 6.5 Container isolation

- Each service must be isolated in Docker containers.

---

## 7. Technology Stack

### Core backend

| Area                           | Required technology / approach |
| ------------------------------ | ------------------------------ |
| Microservices                  | C++                            |
| Cryptography                   | OpenSSL / libsodium            |
| Real-time client communication | Secure WebSockets              |
| Auth API                       | REST                           |
| Database                       | PostgreSQL                     |
| Compression                    | zlib                           |
| Windows IPC                    | Named pipes                    |
| Linux IPC                      | Domain sockets                 |
| Large internal transfers       | Shared memory                  |

### Client

| Area       | Technology            |
| ---------- | --------------------- |
| Desktop UI | Qt                    |
| Platforms  | Windows, Linux, macOS |

### Infrastructure

| Area                    | Technology                 |
| ----------------------- | -------------------------- |
| Containerization        | Docker                     |
| Local orchestration     | Docker Compose             |
| CI/CD                   | Automated pipeline         |
| Source control workflow | Git push triggers pipeline |

---

## 8. Deployment Architecture

### Docker Compose

A single orchestration configuration must start:

- auth-service
- messaging-service
- files-service
- audit-service
- deploy-service
- postgresql

Target command concept:

```bash
docker-compose up
```

The objective is to start the complete SecureCloud stack through centralized orchestration.

---

## 9. CI/CD Pipeline

### Required stages

```text
git push
   ↓
Build
   ↓
Test
   ↓
Package
   ↓
Deploy
   ↓
Verify
```

### Build Stage

- Compile the 5 C++ microservices

### Test Stage

- Automated unit tests
- Automated integration tests

### Package Stage

- Build optimized Docker images

### Deploy Stage

- Deploy to the target environment

### Verify Stage

- Execute post-deployment health checks

### Deployment target

The specification sets the objective:

**git push → production operational within 5 minutes.**

---

## 10. Testing Strategy

### General principle

The project explicitly treats microservices as significantly increasing testing complexity.

Testing must cover:

- Each individual service
- Inter-service communication
- Critical operational scenarios
- Failure conditions
- Deployment verification

### Coverage requirement

The source specification states:

- **100% automated test coverage** as a project deliverable/requirement

This should be treated as a stated target and measured explicitly in the implementation.

---

### 10.1 Auth Service scenarios

#### Mission in Afghanistan

Test:

- Authentication over unstable satellite connectivity

#### Emergency evacuation

Test:

- Immediate access revocation

---

### 10.2 Files Service scenarios

#### Emergency scanner

Test:

- Upload of **500 MB in under 3 minutes**

#### Field backup

Test:

- Backup behavior without internet connectivity

---

### 10.3 Messaging Service scenarios

#### Surgical emergency

Test:

- Absolute message priority

#### Emergency evacuation

Test:

- Immediate broadcast/message delivery to all teams

---

### 10.4 Failure points to test

#### Qt Client

- UI bugs
- Crashes
- Memory leaks

#### API Gateway

- Routing failures
- Rate-limiting bugs

#### Backend services

- Message loss
- Authentication failures
- Service crashes
- File corruption
- Log loss

#### Database

- Data corruption
- Connection loss

---

## 11. Non-Functional Requirements

### Performance

- **10,000 messages/second** at peak
- **< 100 ms response time**
- **500 MB upload < 3 minutes**
- Client startup **< 2 seconds**
- Client memory usage **< 50 MB**

### Availability

- **99.9% minimum uptime**

### Compatibility

- Windows
- Linux
- macOS

### Deployment

- Automated deployment
- Target deployment cycle: **5 minutes from push to operational environment**

### Environment

Must operate under:

- Hostile/remote environments
- Low-bandwidth satellite networks
- 45°C temperatures
- Dusty conditions
- Aging hardware

### Delivery timeline

- **Maximum project duration: 3 months**

---

## 12. Acceptance Criteria

### Performance acceptance

- [ ] 10,000 simultaneous messages per second
- [ ] Response time below 100 ms
- [ ] 500 MB upload below 3 minutes
- [ ] Responsive interface under degraded conditions

### Security acceptance

- [ ] E2E encryption verified by external audit
- [ ] Resistance to brute-force attacks
- [ ] Server cannot intercept plaintext communications
- [ ] Immediate access revocation works

### Usage acceptance

- [ ] MSF team training completed successfully
- [ ] Field deployment validated
- [ ] Emergency scenarios tested
- [ ] User adoption confirmed

---

## 13. Implementation Checklist

### Foundation

- [ ] Set up C++ build environment
- [ ] Define repository structure
- [ ] Configure shared coding standards
- [ ] Configure GoogleTest or equivalent test framework
- [ ] Configure Docker and Docker Compose
- [ ] Configure CI/CD pipeline

### Architecture

- [ ] Implement API Gateway
- [ ] Define service boundaries
- [ ] Implement IPC abstraction
- [ ] Implement service health checks
- [ ] Define PostgreSQL integration
- [ ] Define audit event flow

### Authentication

- [ ] User identity management
- [ ] MFA
- [ ] JWT issuance
- [ ] Token refresh
- [ ] Token validation
- [ ] Permission checks
- [ ] Immediate token revocation

### Messaging

- [ ] 1-to-1 conversations
- [ ] Groups up to 100 users
- [ ] Secure WebSocket communication
- [ ] E2E encryption flow
- [ ] Presence
- [ ] Notifications
- [ ] Conversation threads
- [ ] Local encrypted search indexing

### Files

- [ ] HTTP/HTTPS service
- [ ] Streaming encryption
- [ ] Chunking
- [ ] zlib compression
- [ ] Encrypted virtual filesystem
- [ ] Block deduplication
- [ ] Integrity validation
- [ ] Interrupted-transfer handling

### Client

- [ ] Qt cross-platform application
- [ ] Messaging UI
- [ ] File sharing UI
- [ ] Notifications
- [ ] Degraded-network behavior
- [ ] Memory-leak testing
- [ ] Startup-time measurement

### Infrastructure

- [ ] Dockerfile for each microservice
- [ ] docker-compose configuration
- [ ] Build pipeline
- [ ] Unit tests in pipeline
- [ ] Integration tests in pipeline
- [ ] Docker image packaging
- [ ] Automated deployment
- [ ] Post-deployment verification
- [ ] Rollback strategy

---

## 14. Source-Level Ambiguities to Resolve Before Implementation

The provided documents specify the intended architecture and requirements, but some implementation details are not fully defined and should be resolved by the project team before coding.

### 14.1 Encryption design

The documents require:

- AES-256
- End-to-end encryption
- OpenSSL/libsodium

However, they do not fully define:

- Key exchange protocol
- Key storage model
- Group key management
- Forward secrecy requirements
- Exact authenticated encryption mode
- Device onboarding/recovery

### 14.2 Service communication

The documents require:

- Named pipes on Windows
- Domain sockets on Linux
- Shared memory for large transfers

However, they do not define:

- A cross-platform IPC abstraction
- Message serialization format
- Service discovery mechanism
- Error/retry protocol

### 14.3 API Gateway vs internal IPC

The documents specify both:

- A central API Gateway
- No HTTP between internal services

The exact boundary between:

- Client → Gateway communication
- Gateway → service communication
- Service → service communication

should be explicitly documented.

### 14.4 PostgreSQL architecture

The documents list PostgreSQL but do not specify:

- Schema ownership per service
- Whether services share a database
- Replication/high availability
- Backup and recovery strategy

### 14.5 Offline operation

The source includes a field backup scenario without internet connectivity, but does not fully specify:

- Local storage behavior
- Synchronization strategy
- Conflict resolution
- Offline message queueing

---

## 15. Recommended Implementation Priority

Based strictly on the project requirements, an implementation sequence could be:

### Phase 1 — Foundation

1. Repository and C++ build system
2. Docker environment
3. Service skeletons
4. IPC abstraction
5. PostgreSQL integration
6. Automated testing and CI

### Phase 2 — Security Foundation

1. Authentication service
2. Identity management
3. Permissions
4. JWT lifecycle
5. Revocation
6. Cryptographic library integration

### Phase 3 — Messaging

1. Basic message transport
2. Secure WebSockets
3. Conversation model
4. E2E encryption integration
5. Groups and presence
6. Threads and search

### Phase 4 — File Sharing

1. Upload/download pipeline
2. Chunking
3. Streaming encryption
4. Compression
5. Integrity handling
6. Deduplication

### Phase 5 — Client

1. Qt application shell
2. Authentication UI
3. Messaging UI
4. File UI
5. Notifications
6. Cross-platform validation

### Phase 6 — Audit and Deployment

1. Audit event collection
2. Encrypted logs
3. Docker images
4. Docker Compose
5. CI/CD deployment
6. Health checks and rollback

### Phase 7 — Validation

1. Performance testing
2. Security testing
3. Failure testing
4. Degraded network testing
5. Emergency scenario testing
6. Acceptance criteria validation

---

## 16. Final Implementation Baseline

The implementation baseline defined by the documents is:

> **A cross-platform Qt desktop application backed by five containerized C++ microservices, using PostgreSQL, native inter-service IPC, secure WebSocket client communication, end-to-end encryption, secure streaming file transfer, centralized authentication/authorization, encrypted auditing, and automated Docker-based CI/CD deployment.**

The most critical measurable requirements are:

- **10,000 messages/second**
- **< 100 ms response time**
- **500 MB upload in < 3 minutes**
- **99.9% uptime**
- **Windows/Linux/macOS support**
- **Operation over 256 kbps satellite connections**
- **Client startup < 2 seconds**
- **Client RAM < 50 MB**
- **Automated deployment within 5 minutes**
- **Maximum delivery timeline: 3 months**
