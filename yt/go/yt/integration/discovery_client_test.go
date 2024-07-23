package integration

import (
	"context"
	"strconv"
	"testing"
	"time"

	"github.com/stretchr/testify/require"
	"golang.org/x/exp/slices"

	"go.ytsaurus.tech/library/go/ptr"
	"go.ytsaurus.tech/yt/go/ypath"
	"go.ytsaurus.tech/yt/go/yt"
	"go.ytsaurus.tech/yt/go/yt/ytdiscovery"
	"go.ytsaurus.tech/yt/go/yttest"
)

const leaseTimeoutMicroseconds = 60 * 1000 * 1000

func getDiscoveryClientAndServers(t *testing.T, env *yttest.Env) (yt.DiscoveryClient, string) {
	t.Helper()

	ctx, cancel := context.WithTimeout(env.Ctx, time.Minute)
	defer cancel()

	var servers []string
	err := env.YT.GetNode(ctx, ypath.Path("//sys/@cluster_connection/discovery_connection/addresses"), &servers, nil)
	require.NoError(t, err)

	require.NotEmpty(t, servers)

	client, err := ytdiscovery.NewStatic(&yt.DiscoveryConfig{
		DiscoveryServers: servers,
	})
	require.NoError(t, err)

	var sysFolders []string
	err = env.YT.ListNode(ctx, ypath.Path("//sys"), &sysFolders, nil)
	require.NoError(t, err)

	discoveryServerPath := "//sys/"
	if slices.Contains(sysFolders, "discovery_servers") {
		discoveryServerPath += "discovery_servers/"
	} else {
		discoveryServerPath += "primary_masters/"
	}
	discoveryServerPath += servers[0]

	return client, discoveryServerPath
}

func initMembers(
	t *testing.T,
	ctx context.Context,
	dc yt.DiscoveryClient,
	groupID string,
	count int,
) []*yt.MemberInfo {
	t.Helper()

	members := make([]*yt.MemberInfo, 0, count)
	for i := 1; i <= count; i++ {
		member := yt.MemberInfo{
			ID:       "member" + strconv.Itoa(i),
			Priority: 1,
			Revision: 1,
			Attributes: []*yt.Attribute{
				{Key: "host", Value: []byte("host" + strconv.Itoa(i))},
				{Key: "monitoring_port", Value: []byte("9020")},
			},
		}
		err := dc.Heartbeat(ctx, groupID, member, leaseTimeoutMicroseconds, nil)
		require.NoError(t, err)

		members = append(members, &member)
	}

	return members
}

func TestDiscoveryClient(t *testing.T) {
	t.Parallel()

	env := yttest.New(t)

	dc, discoveryServerPath := getDiscoveryClientAndServers(t, env)
	defer dc.Stop()

	t.Run("P", func(t *testing.T) {
		t.Run("Heartbeat", func(t *testing.T) {
			t.Parallel()

			ctx, cancel := context.WithTimeout(env.Ctx, time.Second*15)
			defer cancel()

			checkGroup := func(groupPath ypath.Path, expectedMemberCount int32) {
				t.Helper()

				var group struct {
					MemberCount int32 `yson:"member_count"`
				}
				err := env.YT.GetNode(ctx, groupPath.Attrs(), &group, nil)
				require.NoError(t, err)

				require.Equal(t, expectedMemberCount, group.MemberCount)
			}

			checkMember := func(groupPath ypath.Path, id string) {
				t.Helper()

				var ids []string
				err := env.YT.ListNode(ctx, groupPath.Attr("members"), &ids, nil)
				require.NoError(t, err)

				ok := slices.Contains(ids, id)
				require.True(t, ok)
			}

			err := dc.Heartbeat(ctx, "/test/heartbeat", yt.MemberInfo{
				ID:       "member1",
				Priority: 1,
				Revision: 1,
				Attributes: []*yt.Attribute{
					{Key: "host", Value: []byte("host1")},
					{Key: "monitoring_port", Value: []byte("9020")},
				},
			}, leaseTimeoutMicroseconds, nil)
			require.NoError(t, err)

			groupPath := ypath.Path(discoveryServerPath + "/orchid/discovery_server/test/heartbeat")

			checkGroup(groupPath, 1)
			checkMember(groupPath, "member1")
		})

		t.Run("GetGroupMeta", func(t *testing.T) {
			t.Parallel()

			ctx, cancel := context.WithTimeout(env.Ctx, time.Second*15)
			defer cancel()

			memberCount := 3
			initMembers(t, ctx, dc, "/test/get_group_meta", memberCount)

			meta, err := dc.GetGroupMeta(ctx, "/test/get_group_meta", nil)
			require.NoError(t, err)
			require.Equal(t, int32(memberCount), meta.MemberCount)
		})

		t.Run("ListMembers", func(t *testing.T) {
			t.Parallel()

			ctx, cancel := context.WithTimeout(env.Ctx, time.Second*15)
			defer cancel()

			memberCount := 3
			initMembers(t, ctx, dc, "/test/get_group_meta", memberCount)

			members, err := dc.ListMembers(ctx, "/test/get_group_meta", &yt.ListMembersOptions{
				Limit: ptr.Int32(10),
			})
			require.NoError(t, err)
			require.Len(t, members, memberCount)
		})
	})
}
