# Abans-Group

## Overview
This project includes a C++ client application that interacts with the ABX mock exchange server over TCP. The application processes stock ticker data and ensures no missing sequences in the final JSON output.

## Setup and Execution

### Server-Side (Node.js)
1. Navigate to the project directory.
2. Run the following command to start the server:
   ```sh
   node main.js
   ```

### Client-Side (C++)
1. Compile the C++ client using g++:
   ```sh
   g++ -o Client Client.cpp
   ```
2. Execute the compiled client:
   ```sh
   ./Client
   ```
3. The client will process the data and store the output in `Output.json`.

## Dependencies
- Node.js (for the server-side script)
- g++ (for compiling the C++ client)
- A Linux-based OS (tested on Ubuntu)

## Output
The processed data will be stored in a file named `Output.json`.

## Notes
- Ensure that the server (`main.js`) is running before starting the client.
- The client application ensures that no data packets are missed in the final JSON output.
