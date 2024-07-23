package jupyt

import (
	"context"

	"go.ytsaurus.tech/library/go/core/log"
	"go.ytsaurus.tech/yt/chyt/controller/internal/strawberry"
	"go.ytsaurus.tech/yt/go/ypath"
	"go.ytsaurus.tech/yt/go/yson"
	"go.ytsaurus.tech/yt/go/yt"
	"go.ytsaurus.tech/yt/go/yterrors"
)

type Config struct {
	YTAuthCookieName *string           `yson:"yt_auth_cookie_name"`
	ExtraEnvVars     map[string]string `yson:"extra_env_vars"`
}

const (
	DefaultYTAuthCookieName = ""
)

func (c *Config) YTAuthCookieNameOrDefault() string {
	if c.YTAuthCookieName != nil {
		return *c.YTAuthCookieName
	}
	return DefaultYTAuthCookieName
}

type Controller struct {
	ytc     yt.Client
	l       log.Logger
	root    ypath.Path
	cluster string
	config  Config
}

func (c *Controller) UpdateState() (changed bool, err error) {
	return false, nil
}

func (c *Controller) buildCommand(speclet *Speclet) (command string, env map[string]string) {
	// TODO(max): come up with a solution how to pass secrets (token or password) without exposing them in the
	// strawberry attributes.

	cmd := "bash -x start.sh /opt/conda/bin/jupyter lab --ip '*' --port $YT_PORT_0 --LabApp.token='' --allow-root >&2"
	jupyterEnv := map[string]string{
		"NB_GID":  "0",
		"NB_UID":  "0",
		"NB_USER": "root",
	}
	for key, value := range c.config.ExtraEnvVars {
		jupyterEnv[key] = value
	}
	return cmd, jupyterEnv
}

func (c *Controller) Prepare(ctx context.Context, oplet *strawberry.Oplet) (
	spec map[string]any, description map[string]any, annotations map[string]any, err error) {
	alias := oplet.Alias()

	// description = buildDescription(c.cluster, alias, c.config.EnableYandexSpecificLinksOrDefault())
	speclet := oplet.ControllerSpeclet().(Speclet)

	description = map[string]any{}

	var filePaths []ypath.Rich

	err = c.prepareCypressDirectories(ctx, oplet.Alias())
	if err != nil {
		return
	}

	err = c.appendConfigs(ctx, oplet, &speclet, &filePaths)
	if err != nil {
		return
	}

	command, env := c.buildCommand(&speclet)

	spec = map[string]any{
		"tasks": map[string]any{
			"jupyter": map[string]any{
				"command":                            command,
				"job_count":                          1,
				"docker_image":                       speclet.JupyterDockerImage,
				"memory_limit":                       speclet.MemoryOrDefault(),
				"cpu_limit":                          speclet.CPUOrDefault(),
				"file_paths":                         filePaths,
				"port_count":                         1,
				"max_stderr_size":                    1024 * 1024 * 1024,
				"user_job_memory_digest_lower_bound": 1.0,
				"environment":                        env,
			},
		},
		"max_failed_job_count": 10 * 1000,
		"max_stderr_count":     150,
		"title":                "JUPYT notebook *" + alias,
	}
	annotations = map[string]any{
		"is_notebook": true,
		"expose":      true,
	}

	return
}

func (c *Controller) Family() string {
	return "jupyt"
}

func (c *Controller) Root() ypath.Path {
	return c.root
}

func (c *Controller) ParseSpeclet(specletYson yson.RawValue) (any, error) {
	var speclet Speclet
	err := yson.Unmarshal(specletYson, &speclet)
	if err != nil {
		return nil, yterrors.Err("failed to parse speclet", err)
	}
	return speclet, nil
}

func (c *Controller) DescribeOptions(parsedSpeclet any) []strawberry.OptionGroupDescriptor {
	speclet := parsedSpeclet.(Speclet)

	return []strawberry.OptionGroupDescriptor{
		{
			Title: "Jupyt params",
			Options: []strawberry.OptionDescriptor{
				{
					Title:        "Docker image",
					Name:         "jupyter_docker_image",
					Type:         strawberry.TypeString,
					CurrentValue: speclet.JupyterDockerImage,
					Description:  "A docker image containing jupyt and required stuff.",
				},
			},
		},
		{
			Title: "Resources",
			Options: []strawberry.OptionDescriptor{
				{
					Title:        "CPU",
					Name:         "cpu",
					Type:         strawberry.TypeInt64,
					CurrentValue: speclet.CPU,
					DefaultValue: DefaultCPU,
					MinValue:     1,
					MaxValue:     100,
					Description:  "Number of CPU cores.",
				},
				{
					Title:        "Total memory",
					Name:         "memory",
					Type:         strawberry.TypeByteCount,
					CurrentValue: speclet.Memory,
					DefaultValue: DefaultMemory,
					MinValue:     2 * gib,
					MaxValue:     300 * gib,
					Description:  "Amount of RAM in bytes.",
				},
			},
		},
	}
}

func (c *Controller) GetOpBriefAttributes(parsedSpeclet any) map[string]any {
	speclet := parsedSpeclet.(Speclet)

	return map[string]any{
		"cpu":          speclet.CPUOrDefault(),
		"memory":       speclet.MemoryOrDefault(),
		"docker_image": speclet.JupyterDockerImage,
	}
}

func (c *Controller) appendConfigs(ctx context.Context, oplet *strawberry.Oplet, speclet *Speclet, filePaths *[]ypath.Rich) error {
	serverConfig := jupytServerConfig{
		YTProxy:          c.cluster,
		YTAuthCookieName: c.config.YTAuthCookieNameOrDefault(),
		YTACOName:        oplet.Alias(),
		YTACONamespace:   c.Family(),
		YTACORootPath:    strawberry.AccessControlNamespacesPath.String(),
	}
	serverConfigYTPath, err := c.uploadConfig(ctx, oplet.Alias(), "server_config.json", serverConfig)
	if err != nil {
		return nil
	}
	*filePaths = append(*filePaths, serverConfigYTPath)
	return nil
}

func parseConfig(rawConfig yson.RawValue) Config {
	var controllerConfig Config
	if rawConfig != nil {
		if err := yson.Unmarshal(rawConfig, &controllerConfig); err != nil {
			panic(err)
		}
	}
	return controllerConfig
}

func NewController(l log.Logger, ytc yt.Client, root ypath.Path, cluster string, rawConfig yson.RawValue) strawberry.Controller {
	c := &Controller{
		l:       l,
		ytc:     ytc,
		root:    root,
		cluster: cluster,
		config:  parseConfig(rawConfig),
	}
	return c
}
