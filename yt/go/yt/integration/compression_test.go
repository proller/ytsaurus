package integration

import (
	"fmt"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"go.ytsaurus.tech/library/go/core/log"
	"go.ytsaurus.tech/library/go/core/log/ctxlog"
	"go.ytsaurus.tech/yt/go/yt"
	"go.ytsaurus.tech/yt/go/yt/ythttp"
	"go.ytsaurus.tech/yt/go/yttest"
)

func TestClientCompression(t *testing.T) {
	t.Parallel()

	env := yttest.New(t)

	for _, codec := range []yt.ClientCompressionCodec{
		yt.ClientCodecDefault,
		yt.ClientCodecGZIP,
		yt.ClientCodecNone,
		yt.ClientCodecSnappy,
		yt.ClientCodecZSTDFastest,
		yt.ClientCodecZSTDDefault,
		yt.ClientCodecZSTDBetterCompression,
		yt.ClientCodecBrotliFastest,
		yt.ClientCodecBrotliDefault,
		// TODO(prime@): fix codec select in HTTP proxy
		// yt.ClientCodecBrotliBestCompression,
	} {
		t.Run(fmt.Sprint(codec), func(t *testing.T) {
			yc, err := ythttp.NewClient(&yt.Config{
				CompressionCodec: codec,
				Logger:           env.L,
			})
			require.NoError(t, err)

			ctx := ctxlog.WithFields(env.Ctx, log.String("subtest_name", t.Name()))

			tmpPath := env.TmpPath()
			_, err = yc.CreateNode(ctx, tmpPath, yt.NodeTable, nil)
			require.NoError(t, err)

			w, err := yc.WriteTable(ctx, tmpPath, nil)
			require.NoError(t, err)
			defer w.Rollback()

			require.NoError(t, w.Write(testRow{Key: "a", Value: "b"}))
			require.NoError(t, w.Commit())

			r, err := yc.ReadTable(ctx, tmpPath, nil)
			require.NoError(t, err)
			defer r.Close()

			assert.True(t, r.Next())
			require.NoError(t, r.Err())
			require.NoError(t, r.Scan(new(testRow)))
			assert.False(t, r.Next())
			require.NoError(t, r.Err())
		})
	}
}
