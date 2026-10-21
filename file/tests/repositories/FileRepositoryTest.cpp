#include <gtest/gtest.h>

#include "file/domain/FileRecord.hpp"
#include "repositories/InMemoryFileRepository.hpp"

using file::domain::FileRecord;
using file::repositories::test::InMemoryFileRepository;

TEST(FileRepository, CreateStartsAsUploading) {
  InMemoryFileRepository repo;

  FileRecord f;
  f.filename = "report.pdf.enc";
  f.size_bytes = 1024;
  f.checksum = "sha256-of-ciphertext";
  f.storage_ref = "files/abc123";
  f.nonce = "nonce";

  FileRecord stored = repo.create(f);
  EXPECT_EQ(stored.status, "UPLOADING");
}

TEST(FileRepository, UpdateStatusToAvailable) {
  InMemoryFileRepository repo;

  FileRecord f;
  f.filename = "x";
  f.checksum = "c";
  f.storage_ref = "r";
  f.nonce = "n";
  FileRecord stored = repo.create(f);

  ASSERT_TRUE(repo.updateStatus(stored.id, "AVAILABLE"));
  auto found = repo.findById(stored.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, "AVAILABLE");
}

TEST(FileRepository, SoftDeleteHidesFromFindById) {
  InMemoryFileRepository repo;

  FileRecord f;
  f.filename = "x";
  f.checksum = "c";
  f.storage_ref = "r";
  f.nonce = "n";
  FileRecord stored = repo.create(f);

  ASSERT_TRUE(repo.softDelete(stored.id));
  EXPECT_FALSE(repo.findById(stored.id).has_value());
}
