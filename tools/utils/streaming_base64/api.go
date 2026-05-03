package streaming_base64

import (
	"fmt"
	"iter"

	"github.com/emmansun/base64"
)

var _ = fmt.Print

type CorruptInputError = base64.CorruptInputError

type StreamingBase64Decoder struct {
	leftover     [4]byte
	num_leftover int
	total_read   int64
}

func wrap_error(err error, chunkOffset int64) error {
	if e, ok := err.(CorruptInputError); ok {
		// CorruptInputError is an int64 representing the relative byte offset
		return CorruptInputError(int64(e) + chunkOffset)
	}
	return err
}

// The size of output buffer needed for the provided size of input
func (s *StreamingBase64Decoder) NeededOutputLen(input_len int) int {
	return ((input_len + s.num_leftover) / 4) * 3
}

// Decode provided input, iterating in chunks. Each chunk is a slice from the
// provided output buffer, which must be at least s.NeededOutputLen() in size.
func (s *StreamingBase64Decoder) Decode(input []byte, output []byte) iter.Seq2[[]byte, error] {
	// Base64 decoding: 4 input bytes -> 3 output bytes.
	// We check if output is large enough for this chunk + any buffered data.
	maxPossibleOutput := s.NeededOutputLen(len(input))
	return func(yield func([]byte, error) bool) {
		if len(output) < maxPossibleOutput {
			yield(nil, fmt.Errorf("output slice too small: need at least %d, got %d", maxPossibleOutput, len(output)))
			return
		}
		currIn := input
		outOffset := 0

		// 1. Handle leftover bytes from previous call
		if s.num_leftover > 0 {
			need := 4 - s.num_leftover
			if len(currIn) >= need {
				copy(s.leftover[s.num_leftover:], currIn[:need])

				// Decode the bridge block
				n, err := base64.StdEncoding.Decode(output[outOffset:], s.leftover[:4])
				if err != nil {
					yield(nil, wrap_error(err, s.total_read-int64(s.num_leftover)))
					return
				}

				if !yield(output[outOffset:outOffset+n], nil) {
					return
				}
				outOffset += n
				currIn = currIn[need:]
				s.total_read += int64(need)
				s.num_leftover = 0
			} else {
				// Still not enough to complete a block
				copy(s.leftover[s.num_leftover:], currIn)
				s.num_leftover += len(currIn)
				s.total_read += int64(len(currIn))
				return
			}
		}

		// 2. Decode the bulk of the current chunk
		processableLen := (len(currIn) / 4) * 4
		if processableLen > 0 {
			if n, err := base64.StdEncoding.Decode(output[outOffset:], currIn[:processableLen]); err != nil {
				yield(nil, wrap_error(err, s.total_read))
				return
			} else if n > 0 {
				if !yield(output[outOffset:outOffset+n], nil) {
					return
				}
			}
			currIn = currIn[processableLen:]
			s.total_read += int64(processableLen)
		}

		// 3. Buffer remaining bytes (1-3) for the next Decode call
		if len(currIn) > 0 {
			copy(s.leftover[:], currIn)
			s.num_leftover = len(currIn)
			s.total_read += int64(len(currIn))
		}
	}
}

// Finish decoding the stream. Resets the decoder. Returned slice can be nil
// if no leftover bytes are present.
func (s *StreamingBase64Decoder) Finish() ([]byte, error) {
	defer func() {
		s.num_leftover = 0
		s.total_read = 0
	}()
	switch s.num_leftover {
	case 0:
		return nil, nil
	case 1:
		return nil, CorruptInputError(s.total_read - 1)
	case 2:
		s.leftover[2] = '='
		s.leftover[3] = '='
	case 3:
		s.leftover[3] = '='
	}
	output := [3]byte{}
	n, err := base64.StdEncoding.Decode(output[:3], s.leftover[:4])
	return output[:n], wrap_error(err, s.total_read-int64(s.num_leftover))
}

type StreamingBase64Encoder struct {
	leftover     [3]byte
	num_leftover int
}

// The size of output buffer needed to encode the provided number of input bytes.
func (s *StreamingBase64Encoder) NeededOutputLen(input_len int) int {
	return ((input_len + s.num_leftover) / 3) * 4
}

// Encode provided input, iterating in chunks. Each chunk is a slice from the
// provided output buffer, which must be at least s.NeededOutputLen() in size.
// The only error returned is when the output slice is too small.
func (s *StreamingBase64Encoder) Encode(input []byte, output []byte) iter.Seq2[[]byte, error] {
	maxPossibleOutput := s.NeededOutputLen(len(input))
	return func(yield func([]byte, error) bool) {
		if len(output) < maxPossibleOutput {
			yield(nil, fmt.Errorf("output slice too small: need at least %d, got %d", maxPossibleOutput, len(output)))
			return
		}
		currIn := input
		outOffset := 0

		// 1. Handle leftover bytes from previous call
		if s.num_leftover > 0 {
			need := 3 - s.num_leftover
			if len(currIn) >= need {
				copy(s.leftover[s.num_leftover:], currIn[:need])

				// Encode the bridge block (3 bytes -> 4 chars)
				base64.RawStdEncoding.Encode(output[outOffset:], s.leftover[:3])
				if !yield(output[outOffset:outOffset+4], nil) {
					return
				}
				outOffset += 4
				currIn = currIn[need:]
				s.num_leftover = 0
			} else {
				// Still not enough to complete a group of 3
				copy(s.leftover[s.num_leftover:], currIn)
				s.num_leftover += len(currIn)
				return
			}
		}

		// 2. Encode the bulk of the current chunk without copying
		processableLen := (len(currIn) / 3) * 3
		if processableLen > 0 {
			encodedLen := (processableLen / 3) * 4
			base64.RawStdEncoding.Encode(output[outOffset:], currIn[:processableLen])
			if !yield(output[outOffset:outOffset+encodedLen], nil) {
				return
			}
			outOffset += encodedLen
			currIn = currIn[processableLen:]
		}

		// 3. Buffer remaining bytes (1-2) for the next Encode call
		if len(currIn) > 0 {
			copy(s.leftover[:], currIn)
			s.num_leftover = len(currIn)
		}
	}
}

// Finish encoding the stream. Resets the encoder. Returned slice can be nil
// if no leftover bytes are present.
func (s *StreamingBase64Encoder) Finish() []byte {
	if s.num_leftover == 0 {
		return nil
	}
	encodedLen := base64.RawStdEncoding.EncodedLen(s.num_leftover)
	output := [4]byte{}
	base64.RawStdEncoding.Encode(output[:encodedLen], s.leftover[:s.num_leftover])
	s.num_leftover = 0
	return output[:encodedLen]
}
