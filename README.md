# deflate for esp32

This project adds a high level interface for deflate to the esp32 based on miniz from the ROM.

The functionality in particular can be useful to perform OTA firmware upgrades

To compress a file one may use the following (and use this library to decompress on the ESP32)

```
python -c "import zlib; import sys; open(sys.argv[2], 'wb').write(zlib.compress(open(sys.argv[1], 'rb').read(), 9))" uncompress_file.bin compressed_file.bin
```

