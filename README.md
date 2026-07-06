# TftpuPkg

TFTP Upload (tftpu) UEFI Shell command for EDK2.

This package provides a standalone UEFI application that allows uploading files from UEFI Shell to a TFTP server.

## Features

- Upload files from UEFI environment to TFTP server
- Supports all standard TFTP options (blksize, windowsize, retry count, timeout)
- Supports specifying network interface, local/remote ports
- Help information via `-h`, `-?`, and `--help` options

## Build

### Prerequisites

- EDK2 source code
- GCC toolchain (tested with GCC5)
- Python 3

### Build Command

```bash
cd /path/to/edk2
source edksetup.sh
build -a X64 -b RELEASE -t GCC5 -p TftpuPkg/TftpuPkg.dsc
```

### Output

The built EFI file will be located at:

```
Build/TftpuPkg/RELEASE_GCC5/X64/tftpu.efi
```

## TFTP Server Setup

### Important Notes

TFTP servers typically **do not allow uploads by default**. You must explicitly configure the server to enable write operations.

### Linux (tftpd-hpa)

```bash
# Install tftpd-hpa
sudo apt install tftpd-hpa

# Configure /etc/default/tftpd-hpa
# Add --create flag to enable uploads
TFTP_OPTIONS="--secure --create"

# Restart service
sudo systemctl restart tftpd-hpa

# Set proper permissions on TFTP directory
sudo chmod 777 /srv/tftp
```

### Linux (atftpd)

```bash
# Install atftpd
sudo apt install atftpd

# Configure with --enable-upload
sudo atftpd --daemon --port 69 --enable-upload /srv/tftp
```

### Windows

1. Open Control Panel → Programs → Turn Windows features on or off
2. Check "TFTP Client" and "TFTP Server"
3. Configure TFTP server properties:
   - Set "TFTP Root Directory"
   - Enable "Allow write requests"

## Usage

### Syntax

```
tftpu [-i interface] [-l <port>] [-r <port>] [-c <retry count>] [-t <timeout>]
      [-s <block size>] [-w <window size>] localfilepath host [remotefilepath]
```

### Options

| Option | Description |
|--------|-------------|
| `-h / -? / --help` | Display help information |
| `-i interface` | Specifies an adapter name (e.g., eth0) |
| `-l port` | Specifies the local port number (default: 0, auto-assigned) |
| `-r port` | Specifies the remote port number (default: 69) |
| `-c <retry count>` | Number of retries (default: 6, 0 means use default) |
| `-t <timeout>` | Timeout in seconds (default: 4) |
| `-s <block size>` | TFTP blksize option (8-65464, default: 512) |
| `-w <window size>` | TFTP windowsize option (1-64, default: 1) |
| `localfilepath` | Local source file path to upload |
| `host` | TFTP Server IPv4 address |
| `remotefilepath` | Optional: destination file path on server |

### Examples

```bash
# Upload file.bin to server 192.168.1.1 using local filename
fs0:\> tftpu file.bin 192.168.1.1

# Upload file2.dat to server 192.168.1.1, store as dir1/file1.dat
fs0:\> tftpu file2.dat 192.168.1.1 dir1/file1.dat

# Upload file using specific network interface
fs0:\> tftpu -i eth0 file.bin 192.168.1.100

# Upload with custom block size and retry count
fs0:\> tftpu -s 1428 -c 10 firmware.efi 10.0.0.5

# Display help
fs0:\> tftpu -h
fs0:\> tftpu -?
fs0:\> tftpu --help
```

## Notes

1. **Network Interface Configuration**: Before using `tftpu`, ensure the network interface is configured with an IP address. Use the `ifconfig` command to configure it:
   ```bash
   fs0:\> ifconfig eth0 192.168.1.10
   ```

2. **Server Error Handling**: If upload fails, the command will display the TFTP server error code and message:
   ```
   TFTP Server Error [2]: Access violation
   ```

3. **Help Options**: Due to EDK2 Shell parser limitations, `--help` is handled differently from `-h` and `-?`. The `-h` and `-?` options are parsed normally, while `--help` is detected by manually checking command line arguments.

4. **TFTP Server Requirements**:
   - Ensure the TFTP server is running and reachable
   - Ensure the target directory has write permissions
   - Ensure the server is configured to allow uploads (e.g., `--create` flag for tftpd-hpa)

## Project Structure

```
TftpuPkg/
├── TftpuPkg.dsc          # Package description file
├── TftpuApp.inf          # Application INF file
├── TftpuApp.c            # Application entry point
├── Tftpu.c               # Main implementation
├── Tftpu.h               # Header file
├── Tftpu.uni             # String resources (help text, error messages)
└── README.md             # This file
```

## License

GNU General Public License v3.0 (GPLv3)