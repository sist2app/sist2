# Outlook mailboxes

Only Outlook writes this format, so these two are taken from the Apache Tika test corpus rather
than generated, and are covered by the Apache License 2.0 rather than the CC0 the rest of these
files are under.

| File                     | Taken from                       | What it covers                                                                  |
|--------------------------|----------------------------------|---------------------------------------------------------------------------------|
| `mailbox.pst`            | Tika `testPST.pst`               | Folder and sender names outside ASCII, a message attached to a message           |
| `various_body_types.pst` | Tika `testPST_variousBodyTypes.pst` | The same message written as plain text, as HTML, as RTF, and with no body at all |

Both are at
`tika-parsers/tika-parsers-standard/tika-parsers-standard-modules/tika-parser-microsoft-module/src/test/resources/test-documents/`
in https://github.com/apache/tika.
