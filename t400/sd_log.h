#ifndef SD_LOG_H
#define SD_LOG_H

namespace sd {

// Initialize the SD card
void init();

// Open a file for logging
// @param fileName File name to save to. If the file already exists, the name will be iterated until
//        an unused file is found.
// @return True if the file could be opened, false otherwise
bool open(char* fileName);

// Close the file on the SD card and disconnect from it
// Call this before powering down the board
void close();

// Log a message to the SD card (one complete row)
bool log(char* message);

// Write part of a row to the SD card (no line ending). Lets a row be assembled from small pieces
// instead of one large RAM buffer.
void write(const char* text);

// End the current row: line ending, periodic flush.
// @return True if everything written so far succeeded
bool endRow();

// Flush the SD card data to disk
// @param force If true, force the data to be synced
void sync(boolean force);

}

#endif
