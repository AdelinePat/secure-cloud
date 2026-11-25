#include "file/storage/ChunkStorage.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>

#include "TempDir.hpp"

using file::storage::FilesystemChunkStorage;

namespace {

const std::string kFileId = "0a1b2c3d-0000-4000-8000-000000000001";

}  // namespace

TEST(ChunkStorage, WriteThenReadReturnsSameBytes) {
  file::test::TempDir dir;
  FilesystemChunkStorage storage(dir.path());

  const std::string bytes("\x00\x01\xff binary\n", 11);
  storage.write(kFileId, 0, bytes);

  auto read = storage.read(kFileId, 0);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, bytes);
  EXPECT_TRUE(std::filesystem::exists(dir.path() / kFileId / "0"));
  EXPECT_FALSE(std::filesystem::exists(dir.path() / kFileId / "0.tmp"));
}

TEST(ChunkStorage, RewriteReplacesChunk) {
  file::test::TempDir dir;
  FilesystemChunkStorage storage(dir.path());

  storage.write(kFileId, 3, "first version, longer");
  storage.write(kFileId, 3, "second");
  EXPECT_EQ(storage.read(kFileId, 3), "second");
}

TEST(ChunkStorage, ReadMissingChunkReturnsNullopt) {
  file::test::TempDir dir;
  FilesystemChunkStorage storage(dir.path());

  EXPECT_FALSE(storage.read(kFileId, 0).has_value());
  storage.write(kFileId, 0, "x");
  EXPECT_FALSE(storage.read(kFileId, 1).has_value());
}

TEST(ChunkStorage, RemoveFileDeletesAllChunks) {
  file::test::TempDir dir;
  FilesystemChunkStorage storage(dir.path());

  storage.write(kFileId, 0, "a");
  storage.write(kFileId, 1, "b");
  storage.removeFile(kFileId);

  EXPECT_FALSE(std::filesystem::exists(dir.path() / kFileId));
  EXPECT_NO_THROW(storage.removeFile(kFileId));  // already gone is fine
}

TEST(ChunkStorage, RejectsIdsThatCouldEscapeRoot) {
  file::test::TempDir dir;
  FilesystemChunkStorage storage(dir.path());

  EXPECT_THROW(storage.write("../../etc/passwd", 0, "x"), std::invalid_argument);
  EXPECT_THROW(storage.write("../0a1b2c3d-0000-4000-8000-00000000001", 0, "x"),
               std::invalid_argument);
  EXPECT_THROW(storage.removeFile(""), std::invalid_argument);
  EXPECT_THROW(storage.write(kFileId, -1, "x"), std::invalid_argument);
}
