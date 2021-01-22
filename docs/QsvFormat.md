# The `.qsv` container

What the iQIYI desktop client writes when it downloads a video, as far as it has
been worked out. This is the prose behind [`src/qsv/types.h`](../src/qsv/types.h)
and [`src/qsv/crypto.cc`](../src/qsv/crypto.cc); the offsets in the tables
below are the offsets those files use.

The analysis is inherited from the [original project](https://github.com/btnkij/qsv2flv),
whose author reverse-engineered it. Fields marked *unknown* are still unknown —
nothing here reads them, and nothing needs to. If you know what one of them is,
or a newer client changes something, please open an issue.

Nothing in a `.qsv` is encrypted in a cryptographic sense. It is obfuscation:
enough to stop a file being renamed to `.flv` and played, and not enough to be
worth describing as a cipher. Both schemes are in
[`crypto.cc`](../src/qsv/crypto.cc), in about thirty lines.

## Shape

```
0x0000  Header                    fixed, 0x5A bytes
0x005A  Index flags               ceil(index_count / 8) bytes, one bit each
        Index table               index_count * 0x1C bytes
        Segment 0                 first 0x400 bytes obfuscated, rest verbatim
        Segment 1
        ...
        XML section               somewhere past the segments; unread
```

A file holds several **segments**, each a complete, standalone video stream with
its own container header — FLV in files from older clients, MPEG-TS in newer
ones. Converting a `.qsv` means demuxing each segment in turn and writing all of
their packets into one output container. That is a remux; no frame is ever
decoded.

## Header

| Offset | Size | Field        | Notes |
|--------|------|--------------|-------|
| `0x00` | `0x0A` | `signature`  | ASCII `QIYI VIDEO`, no terminator |
| `0x0A` | `0x04` | `version`    | `1` or `2`; decides which obfuscation is used |
| `0x0E` | `0x10` | `vid`        | Video id |
| `0x1E` | `0x04` | `_unknown1`  | Always `1` |
| `0x22` | `0x20` | `_unknown2`  | Always zero |
| `0x42` | `0x04` | `_unknown3`  | Unknown |
| `0x46` | `0x04` | `_unknown4`  | Unknown |
| `0x4A` | `0x08` | `xml_offset`  | Absolute offset of the XML section |
| `0x52` | `0x04` | `xml_size`    | Size of the XML section |
| `0x56` | `0x04` | `index_count` | Number of index records |

All multi-byte fields are little-endian.
[`qsv::ParseHeader()`](../src/qsv/types.cc) decodes them byte by byte rather
than reading a packed struct, so neither the host's byte order nor the
compiler's packing behaviour is part of the contract.

## Index flags

`ceil(index_count / 8)` bytes immediately after the header, one bit per segment.
Purpose unknown. `qsv::Reader` seeks past them.

## Index table

`index_count` records of `0x1C` bytes each. **In a version 2 file each record is
stored under `DecryptV2`; in a version 1 file it is in the clear.**

| Offset | Size | Field           | Notes |
|--------|------|-----------------|-------|
| `0x00` | `0x10` | `_codetable`    | Consumed by `DecryptV2`; not otherwise used |
| `0x10` | `0x08` | `segment_offset` | Absolute file offset of the segment |
| `0x18` | `0x04` | `segment_size`   | Size of the segment in bytes |

Both fields come from a file that may be corrupt or hostile, so
[`qsv::Reader::Parse()`](../src/qsv/reader.cc) checks each record against the
file's real length before it is used, and does the check by subtraction — the
obvious `offset + size > file_size` wraps for an offset near the top of the
64-bit range, passes validation, and is then seeked to.

## Segments

A segment is a complete video stream. Its **first `0x400` bytes are obfuscated**
and everything after them is verbatim. `segment_size` is therefore never smaller
than `0x400`, which is one of the things a container has to satisfy to be
accepted.

Timestamps in a real file run continuously from one segment to the next rather
than restarting at zero — which matters, because each segment is demuxed on its
own and arrives with its own idea of where zero is. See
[Architecture.md § Joining segments](Architecture.md#joining-segments) for what
the converter does about it, including the case where a segment *does* restart.

### Version 1 obfuscation

XOR each byte with a repeating four-byte pad, read backwards:

```c
static const uint8_t pad[] = {0x62, 0x67, 0x70, 0x79};   // "bgpy"
buffer[i] ^= pad[~i & 3];
```

Being an XOR, it is its own inverse. That single property is what makes the
whole test suite possible — see [Testing.md](Testing.md).

### Version 2 obfuscation

A shuffle driven by a 32-bit state folded out of the buffer, starting from
`0x62677079` — the same four bytes read as a word. One pass reads the buffer
high index to low to build the state; a second pass walks it low to high,
advancing the state one byte at a time and swapping position `i` with the
position the state selects.

The state is derived from the *ciphertext*, so the sequence of swaps is fixed
before any of them happen. That makes decryption a straight function of the
input and encryption something you would have to solve for: recovering the
initial state from a chosen plaintext means a search that branches at every one
of `size` steps. There is no `encryptV2` in this repository, and
[Testing.md § What version 2 gets instead](Testing.md#what-version-2-gets-instead)
explains what the suite does in place of one.

## XML section

An obfuscated XML blob with binary data at either end, at `xml_offset`. Not
analysed, and not read by anything here — the index table is enough to find
every segment.
