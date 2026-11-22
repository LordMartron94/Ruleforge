package update

import "bytes"

func bytesReaderAt(data []byte) *bytes.Reader { return bytes.NewReader(data) }
