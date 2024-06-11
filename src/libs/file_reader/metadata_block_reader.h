#pragma once
#include "file_structs.h"

/**
 * Reads disk metadata from a Macrium Reflect X backup file.
 *
 * @param fileHandle A handle to the Macrium Reflect X backup file from which to read the metadata.
 * @param Disk A reference to a DiskLayout object where the disk metadata will be stored.
 */
void readDiskMetadata(const file_structs::fileLayout& parsedFileLayout, std::fstream* fileHandle, file_structs::Disk::DiskLayout& Disk);

/**
 * Reads file metadata from a Macrium Reflect X backup file.
 *
 * @param fileHandle A handle to the Macrium Reflect X backup file from which to read the metadata.
 * @param strJSON A reference to a string where the JSON metadata will be stored.
 */
void readFileMetadataData(const file_structs::fileLayout& backupLayout, std::fstream* fileHandle, std::string& strJSON);

/**
 * Skips metadata blocks in a Macrium Reflect X backup file.
 *
 * This function iterates over the metadata blocks and advances the file pointer,
 * but it does not read any block data.
 *
 * @param fileHandle A handle to the Macrium Reflect X backup file from which to skip the metadata.
 */
void skipMetadata(std::fstream* fileHandle);