#include <gtest/gtest.h>

#include "file/domain/FileRecord.hpp"
#include "repositories/InMemoryFileRepository.hpp"

using file::domain::FileChunk;
using file::domain::FileRecord;
using file::repositories::test::InMemoryFileRepository;

namespace {

FileRecord sampleFile() {
  FileRecord f;
  f.conversation_id = "conv-1";
  f.user_id = "user-1";
  f.size_bytes = 3 * 1024;
  f.chunk_count = 3;
  return f;
}

}  // namespace

TEST(FileRepository, CreateStartsAsUploading) {
  InMemoryFileRepository repo;

  FileRecord stored = repo.create(sampleFile());

  EXPECT_FALSE(stored.id.empty());
  EXPECT_EQ(stored.status, "UPLOADING");
  EXPECT_EQ(stored.conversation_id, "conv-1");
  EXPECT_EQ(stored.chunk_count, 3);
  EXPECT_EQ(stored.encryption_algorithm, "xchacha20poly1305-secretstream");
  EXPECT_FALSE(stored.completed_at.has_value());
}

TEST(FileRepository, UpdateStatusToAvailableSetsCompletedAt) {
  InMemoryFileRepository repo;
  FileRecord stored = repo.create(sampleFile());

  ASSERT_TRUE(repo.updateStatus(stored.id, "AVAILABLE"));
  auto found = repo.findById(stored.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, "AVAILABLE");
  EXPECT_TRUE(found->completed_at.has_value());
}

TEST(FileRepository, UpdateStatusOfUnknownFileFails) {
  InMemoryFileRepository repo;
  EXPECT_FALSE(repo.updateStatus("missing", "AVAILABLE"));
}

TEST(FileRepository, SoftDeleteHidesFromFindById) {
  InMemoryFileRepository repo;
  FileRecord stored = repo.create(sampleFile());

  ASSERT_TRUE(repo.softDelete(stored.id));
  EXPECT_FALSE(repo.findById(stored.id).has_value());
  EXPECT_FALSE(repo.softDelete(stored.id));
  EXPECT_FALSE(repo.updateStatus(stored.id, "AVAILABLE"));
}

TEST(FileRepository, ChunksAreListedInIndexOrder) {
  InMemoryFileRepository repo;
  FileRecord stored = repo.create(sampleFile());

  repo.upsertChunk({stored.id, 2, 1024, "cc"});
  repo.upsertChunk({stored.id, 0, 1024, "aa"});

  auto chunks = repo.listChunks(stored.id);
  ASSERT_EQ(chunks.size(), 2u);
  EXPECT_EQ(chunks[0].index, 0);
  EXPECT_EQ(chunks[1].index, 2);
}

TEST(FileRepository, ReuploadedChunkReplacesPrevious) {
  InMemoryFileRepository repo;
  FileRecord stored = repo.create(sampleFile());

  repo.upsertChunk({stored.id, 0, 1000, "old"});
  repo.upsertChunk({stored.id, 0, 1024, "new"});

  auto chunks = repo.listChunks(stored.id);
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].size_bytes, 1024);
  EXPECT_EQ(chunks[0].sha256, "new");
}

TEST(FileRepository, ChunksAreScopedToTheirFile) {
  InMemoryFileRepository repo;
  FileRecord a = repo.create(sampleFile());
  FileRecord b = repo.create(sampleFile());

  repo.upsertChunk({a.id, 0, 10, "a0"});
  repo.upsertChunk({b.id, 0, 10, "b0"});
  repo.upsertChunk({b.id, 1, 10, "b1"});

  EXPECT_EQ(repo.listChunks(a.id).size(), 1u);
  EXPECT_EQ(repo.listChunks(b.id).size(), 2u);
}

TEST(FileRepository, ParticipantRoleLookup) {
  InMemoryFileRepository repo;
  repo.addParticipant("conv-1", "user-1", "admin");

  EXPECT_EQ(repo.findParticipantRole("conv-1", "user-1"), "admin");
  EXPECT_FALSE(repo.findParticipantRole("conv-1", "user-2").has_value());
  EXPECT_FALSE(repo.findParticipantRole("conv-2", "user-1").has_value());
}
