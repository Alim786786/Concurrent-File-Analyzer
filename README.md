# Parallel File Analyzer

This project is a parallel file analyzer written in C. It distributes file analysis tasks (such as counting lines, words, and characters) across multiple worker processes, managed by a controller process. The project demonstrates inter-process communication (IPC), process management, and error handling in a UNIX environment.

## Features
- Spawns multiple worker processes to analyze files in parallel
- Uses pipes for IPC between controller and workers
- Handles worker failures and task re-queuing
- Aggregates results and prints a summary (lines, words, chars)

## File Structure
- `main.c`: Entry point, parses arguments and starts the controller
- `controller.c/h`: Controller logic, worker management, task dispatching
- `worker.c/h`: Worker logic, file analysis
- `analysis.c/h`: File analysis functions (counting lines, words, chars)
- `ipc.c/h`: IPC utility functions (read/write wrappers, error handling)
- `protocol.h`: Message structures and constants for IPC
- `Makefile`: Build instructions
- `demo1.txt`, `demo2.txt`, `demo3.txt`, `video.txt`: Sample input files

## Build

To build the project, run:

```
make
```

## Usage

Run the program with a list of files to analyze and the number of worker processes:

```
./main <num_workers> <file1> <file2> ...
```

Example:
```
./main 3 demo1.txt demo2.txt demo3.txt
```

## Output
- For each file, prints the number of lines, words, and characters
- Prints a summary at the end (successful/failed files, totals)

## Notes
- Handles worker crashes and reassigns unfinished tasks
- Designed for UNIX-like systems (Linux, macOS)

## License
MIT License (or specify your license here)
