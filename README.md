# Open Source  NBL BruteForce - High Performance Directory Brute Forcer
## Might Trigger Anti-DDoS Or Rate-Limit Systems Due To High Req/s 
## Tested On Termux (Lowest: 50 Req/s Highest:2200's Req/s )
## There Might Have Some Errors/Confusions In the code Especially: If The URL Redirects Everything Even If it Doesnt exist The Code Will Save it into Output File

A multi-threaded, high-performance directory and file brute force tool written in C.

## Features

- Multi-threaded architecture for maximum performance
- Connection pooling and keep-alive support
- Smart retry mechanism with configurable attempts
- Atomic operations for thread-safe counting
- Memory pooling for efficient resource management
- URL encoding for special characters
- Flexible output options
- Real-time progress statistics
- Support for common HTTP success codes (200, 301, 302, 403, etc.)
- Configurable timeouts and user agents

## Requirements

### Dependencies
- libcurl development libraries
- pthread (usually included in system)

### Installation on Different Platforms

#### Ubuntu/Debian
sudo apt update
sudo apt install build-essential libcurl4-openssl-dev clang

#### CentOS/RHEL

sudo yum groupinstall 'Development Tools'
sudo yum install libcurl-devel

#### Arch Linux

sudo pacman -S base-devel curl

#### Termux (Android)

pkg install clang curl-dev

Building

Using Makefile (Recommended)

make

Alternative Build Options

make debug      # Build with debug symbols
make static     # Build static binary
make clean      # Clean build artifacts

Manual Compilation

clang -O3 -o nbl nbl.c -lcurl -lpthread

Usage

Basic Syntax

./nbl -u <URL> -w <WORDLIST> [OPTIONS]

Required Parameters

· -u URL        Target URL (must contain NBL placeholder)
· -w WORDLIST   Path to wordlist file

Optional Parameters

· -t THREADS    Number of threads (default: 20, max: 100)
· -o OUTPUT     Output file to save working URLs
· -v            Verbose mode (show response details)
· -a            Show all results (including non-working URLs)
· -e EXTENSIONS File extensions to try (comma-separated)
. -f FORBIDDEN_OUTPUT Output file to save forbidden URLs 
Examples

Basic Directory Discovery

./nbl -u "http://target.com/NBL" -w common_paths.txt

High-Thread Scanning

./nbl -u "http://target.com/NBL" -w big_wordlist.txt -t 80

Save Results to File

./nbl -u "http://target.com/NBL" -w wordlist.txt -o results.txt

Save Results And Forbidden Results To File's
./nbl -u "http://target.com/NBL" -w wordlist.txt -o results.txt -f forbidden_results.txt

Verbose Mode with All Results

./nbl -u "http://target.com/NBL" -w wordlist.txt -v -a

With File Extensions

./nbl -u "http://target.com/NBL" -w wordlist.txt -e ".php,.html,.txt" -o results.txt

URL Placeholder

The tool uses NBL as a placeholder in the URL which gets replaced with words from your wordlist.

Examples:

· http://site.com/NBL becomes http://site.com/admin
· http://site.com/api/NBL becomes http://site.com/api/v1
· http://site.com/NBL.php becomes http://site.com/login.php

Wordlist Format

Wordlist should be a text file with one entry per line. Empty lines and lines starting with # are ignored.

Example wordlist:


admin
login
dashboard
api
test
config
backup
uploads

Output

Console Output

The tool provides real-time progress information:

(Example) Time 15s | Statistics 1250/5000 (25%) | 8 Found | 83 Req/s

File Output

When using -o parameter, only working URLs (status 200, 301, 302, 403, etc.) are saved to the output file.

Success Status Codes

The following HTTP status codes are considered "working" URLs:

· 200 (OK)
· 201 (Created)
· 202 (Accepted)
· 204 (No Content)
· 301 (Moved Permanently)
· 302 (Found)
· 307 (Temporary Redirect)
· 308 (Permanent Redirect)
· 403 (Forbidden)

Performance Tuning

Thread Configuration

· Start with 20-30 threads for general use
· Increase to 50-80 threads for high-speed scanning
· Monitor system resources when using high thread counts

Network Optimization

· The tool uses TCP FastOpen and Nagle's algorithm disable
· Connection timeout: 5 seconds
· Maximum retries: 2

Memory Management

· Memory pooling for URL storage
· Buffered file I/O for wordlist reading
· Automatic cleanup on exit

Error Handling

Common Exit Codes

· 0 - Success
· 1 - Parameter error
· 2 - Wordlist error (file not found or empty)
· 3 - cURL initialization error

Error Messages

· "Failed To Open WordList (Possibly Not Found)" - Wordlist file cannot be opened
· "URL and Wordlist Required" - Missing required parameters
· "Wordlist Empty Or failed to read!" - Wordlist is empty or unreadable

Signal Handling

The tool gracefully handles interrupts:

· SIGINT (Ctrl+C) - Stops scanning and shows statistics
· SIGTERM - Clean shutdown

Security Features

· URL encoding for special characters
· Configurable connection timeouts
· User agent spoofing
· SSL verification disabled for testing
· Buffer size limits to prevent overflows

Recommended Wordlists

· SecLists: https://github.com/danielmiessler/SecLists
· DirBuster Wordlists: Kali Linux package
· Common directories from various security tools
. Already Using https://github.com/danielmiessler/SecLists More Could Be added

Limitations

· Maximum URL length: 4096 characters
· Maximum word length: 512 characters
· Maximum threads: 400
· Maximum retries: 20 per request

Troubleshooting

Build Issues

· Ensure libcurl development packages are installed
· Check pthread availability on your system
· Verify compiler compatibility

Runtime Issues

· Check network connectivity to target
· Verify wordlist file permissions
· Monitor system resource usage with high thread counts

Legal Notice

This tool is intended for security testing and educational purposes only. Only use on systems you own or have explicit permission to test. The authors are not responsible for misuse of this software.

Version Information

Current version: 1.0
Compiled with:C11 standard
Dependencies:libcurl, pthread
