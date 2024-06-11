#include "pch.h"
#include "..\file_operations\file_operations.h"
#include ".\zstd\zstd.h"
#include "..\encryption\encryption.h"
#include "file_reader.h"
#include "metadata_block_reader.h"
#include "metadataheader.h"

/**
 * @file
 * @brief This file contains the implementation of the Macrium Reflect X backup
 * file reader library, specifically for reading and interpreting metadata blocks.
 *
 * This library provides a set of cross-platform functions for reading metadata
 * blocks from a Macrium Reflect X backup file. It handles various aspects such
 * as reading the header offset and magic data, reading metadata blocks, and
 * reading the JSON data from the backup file. It also checks for encryption and
 * compression flags, decompresses data if necessary, decrypts data when required,
 * and computes and verifies the MD5 hash of the data.
 *
 * The functions in this file are implemented in a way that they can be used on
 * different operating systems without requiring system-specific implementations.
 * This makes the code more maintainable and scalable, and allows it to be used
 * in a wider range of environments.
 */


 /**
  * @brief Reads a metadata block from a Macrium Reflect X backup file.
  *
  * The size of the block is determined by `Header.BlockLength`.
  * If the block is compressed, it is decompressed using Zstandard (ZSTD) decompression.
  * If the block is encrypted, the function currently asserts false as encryption
  * is not yet implemented.
  *
  * @param Header A reference to a `MetadataBlockHeader` object that contains
  * the block's metadata.
  * @param hFile A handle to the Macrium Reflect X backup file from which to
  * read the block.
  * @return A `std::unique_ptr` to an array of unsigned chars that contains the
  * block data. If the block length is 0, or if an error occurs during reading
  * or decompression, it returns `nullptr`.
  */

std::unique_ptr<unsigned char[]> readBlock(const file_structs::fileLayout& backupLayout, MetadataBlockHeader& Header, std::fstream* fileHandle)
{
	// If the block length is 0, return nullptr
	if (Header.BlockLength == 0) {
		return nullptr;
	}

	// Allocate a buffer to read the block
	std::unique_ptr<unsigned char[]> readBuffer = std::make_unique<unsigned char[]>(Header.BlockLength);

	// Read the block from the file
	readFile(fileHandle, readBuffer.get(), Header.BlockLength);
	
	// If the block is encrypted, decrypt it using AES decryption in ECB mode.
	// The AES variant and derived key are obtained from the backup layout.
	// The decrypted data replaces the original data in the read buffer.
	if (Header.Flags.Encryption) {
		int aes_variant = backupLayout._encryption.getAESValue();
		decryptDataWithAESECB(aes_variant, backupLayout._encryption.derived_key.data(), readBuffer.get(), Header.BlockLength);
	}

	// If the block is compressed, decompress it
	if (Header.Flags.Compression) {
		// Get the size of the compressed block
		auto compressedsize = ZSTD_findFrameCompressedSize(readBuffer.get(), Header.BlockLength);
		// Get the size of the decompressed block
		auto decompressedsize = ZSTD_getFrameContentSize(readBuffer.get(), Header.BlockLength);

		// Allocate a buffer for the decompressed data
		std::unique_ptr<unsigned char[]> outBuffer = std::make_unique<unsigned char[]>(decompressedsize);

		// Decompress the block
		size_t zout = ZSTD_decompress(outBuffer.get(), (size_t)decompressedsize, readBuffer.get(), compressedsize);

		// If decompression failed, throw an exception
		if (ZSTD_isError(zout)) {
			throw std::runtime_error("Failed to decompress block.");
		}

		// update the block length to the decompressed size
		Header.BlockLength = (uint32_t)decompressedsize;

		// Compute the MD5 hash of the decompressed data
		auto computedHash = computeMD5Hash(outBuffer.get(), decompressedsize);

		// If the computed hash does not match the expected hash, throw an exception
		if (memcmp(computedHash.data(), Header.Hash, sizeof(Header.Hash)) != 0) {
			throw std::runtime_error("Block hash mismatch.");
		}

		// Replace the read buffer with the decompressed data
		readBuffer = std::move(outBuffer);
	}

	// Return the read (and possibly decompressed) data
	return readBuffer;
}


/**
 * @brief Reads disk metadata from a Macrium Reflect X backup file.
 *
 * This function reads the metadata block by block until it finds the
 * TRACK_0 and EXT_PAR_TABLE blocks. It then reads these blocks and
 * assigns their contents to the `Disk` object.
 *
 * @param fileHandle A handle to the Macrium Reflect X backup file from
 * which to read the metadata.
 * @param Disk A reference to a DiskLayout object where the disk metadata
 * will be stored.
 */
void readDiskMetadata(const file_structs::fileLayout& backupLayout, std::fstream* fileHandle, file_structs::Disk::DiskLayout& Disk) {
	std::string strJSON;
	MetadataBlockHeader Header;

	// Get the current file offset
	std::streamoff currentFilePosition = getCurrentFileOffset(fileHandle);

	// Loop until the last block is found
	do {
		// Read the header of the next block
		readFile(fileHandle, &Header, sizeof(Header));

		// If the block name is TRACK_0
		if (memcmp(Header.BlockName, TRACK_0, BLOCK_NAME_LENGTH) == 0) {
			if (Header.BlockLength > 0) {
				auto blockData = readBlock(backupLayout, Header, fileHandle);

				// If reading the block failed, throw an exception
				if (blockData == nullptr) {
					throw std::runtime_error("Failed to read TRACK_0 block.");
				}

				// Assign the block data to the track0 field of the Disk object
				Disk.track0.assign(blockData.get(), blockData.get() + Header.BlockLength);
			}
		}
		else if (memcmp(Header.BlockName, EXT_PAR_TABLE, BLOCK_NAME_LENGTH) == 0) {
			// If the block name is EXT_PAR_TABL
			if (Header.BlockLength > 0) {
				// Read the block
				auto blockData = readBlock(backupLayout, Header, fileHandle);

				// If reading the block failed, throw an exception
				if (blockData == nullptr) {
					throw std::runtime_error("Failed to read EXT_PAR_TABLE block.");
				}

				// Get a pointer to the start of the data in the block
				unsigned char* pDataStart = blockData.get();
				pDataStart += sizeof(uint32_t);

				// Calculate the number of ExtendedPartition objects in the block
				uint32_t extendedPartitionCount = (Header.BlockLength - sizeof(uint32_t)) / sizeof(ExtendedPartition);

				// For each ExtendedPartition object in the block
				for (uint32_t i = 0; i < extendedPartitionCount; i++) {
					// Create an ExtendedPartition object
					ExtendedPartition extendedPartition;

					// Copy the data from the block to the ExtendedPartition object
					memcpy(&extendedPartition, pDataStart, sizeof(ExtendedPartition));

					// Add the ExtendedPartition object to the extendedPartitions field of the Disk object
					Disk.extendedPartitions.push_back(extendedPartition);

					// Move the data pointer to the next ExtendedPartition object in the block
					pDataStart += sizeof(ExtendedPartition);
				}
			}
		}
		else {
			// Move the file pointer to the next block
			currentFilePosition += Header.BlockLength + sizeof(Header);
			setFilePointer(fileHandle, currentFilePosition, std::ios::beg);
		}
	} while (Header.Flags.LastBlock == 0);

	return;
}



/**
 * Reads file metadata from a Macrium Reflect X backup file.
 *
 * This function reads the metadata block by block until it finds the JSON_HEADER block.
 * It then reads this block and assigns its contents to the `strJSON` string.
 *
 * @param fileHandle A handle to the Macrium Reflect X backup file from which to read the metadata.
 * @param strJSON A reference to a string where the JSON metadata will be stored.
 */
void readFileMetadataData(const file_structs::fileLayout& backupLayout, std::fstream* fileHandle, std::string& strJSON) {
	MetadataBlockHeader Header;

	// Get the current file offset
	std::streamoff currentFilePosition = getCurrentFileOffset(fileHandle);

	// Loop until the last block is found
	do {
		// Read the header of the next block
		readFile(fileHandle, &Header, sizeof(Header));

		// If the block name is JSON_HEADER
		if (memcmp(Header.BlockName, JSON_HEADER, BLOCK_NAME_LENGTH) == 0) {
			// Read the block
			auto blockData = readBlock(backupLayout, Header, fileHandle);

			// If reading the block failed, throw an exception
			if (blockData == nullptr) {
				throw std::runtime_error("Failed to read JSON_HEADER block.");
			}
			// Assign the block data to the strJSON string
			strJSON.assign(reinterpret_cast<const char*>(blockData.get()), Header.BlockLength);
		}
		else {
			// Move the file pointer to the next block
			currentFilePosition += Header.BlockLength + sizeof(Header);
			setFilePointer(fileHandle, currentFilePosition, std::ios::beg);
		}
	} while (Header.Flags.LastBlock == 0);

	return;
}

/**
 * Skips metadata blocks in a Macrium Reflect X backup file.
 *
 * This function iterates over the metadata blocks and advances the file pointer,
 * but it does not read any block data.
 *
 * @param fileHandle A handle to the Macrium Reflect X backup file from which to skip the metadata.
 */
void skipMetadata(std::fstream* fileHandle) {
	MetadataBlockHeader Header;

	// Get the current file offset
	std::streamoff currentFilePosition = getCurrentFileOffset(fileHandle);

	// Loop until the last block is found
	do {
		// Read the header of the next block
		readFile(fileHandle, &Header, sizeof(Header));

		// Move the file pointer to the next block
		currentFilePosition += Header.BlockLength + sizeof(Header);
		setFilePointer(fileHandle, currentFilePosition, std::ios::beg);
	} while (Header.Flags.LastBlock == 0);

	return;
}