from yt_commands import (
    create, create_access_control_object_namespace, create_access_control_object, create_user,
    remove_user, remove, add_member, sync_create_cells, sync_remove_tablet_cells, ls,
    set, wait, get)

from yt_queries import get_query_tracker_info

from yt.environment import ExternalComponent

from yt.common import YtError

import pytest


pytest_plugins = [
    "yt.test_helpers.authors",
    "yt.test_helpers.set_timeouts",
    "yt.test_helpers.filter_by_category",
    "yt.test_helpers.fork_class"
]


class QueryTracker(ExternalComponent):
    LOWERCASE_NAME = "query_tracker"
    DASHED_NAME = "query-tracker"
    PLURAL_HUMAN_READABLE_NAME = "query trackers"

    @staticmethod
    def get_default_config():
        return {
            "user": "query_tracker",
            "create_state_tables_on_startup": True,
        }

    def wait_for_readiness(self, address):
        wait(lambda: get(f"//sys/query_tracker/instances/{address}/orchid/service/version",
                         verbose=False, verbose_error=False),
             ignore_exceptions=True)

    def on_start(self):
        query_tracker_config = {
            "stages": {
                "production": {
                    "channel": {
                        "addresses": self.addresses,
                    }
                },
            },
        }
        set("//sys/clusters/primary/query_tracker", query_tracker_config)
        wait(query_tracker_has_loaded)

    def on_finish(self):
        remove("//sys/query_tracker/instances", recursive=True, force=True)
        remove("//sys/clusters/primary/query_tracker")


def query_tracker_has_loaded():
    try:
        return get_query_tracker_info().get("cluster_name") is not None
    except YtError:
        return False


@pytest.fixture
def query_tracker_environment():
    create_user("query_tracker")
    add_member("query_tracker", "superusers")
    sync_create_cells(1)

    create("document", "//sys/query_tracker/config", recursive=True, force=True, attributes={"value": {}})
    create_access_control_object_namespace("queries")
    create_access_control_object("nobody", "queries")
    yield
    remove("//sys/access_control_object_namespaces/queries/nobody")
    remove("//sys/access_control_object_namespaces/queries")
    sync_remove_tablet_cells(ls("//sys/tablet_cells"))
    remove_user("query_tracker")
    remove("//sys/query_tracker", recursive=True, force=True)


def update_query_tracker_environment(cls):
    if hasattr(cls, "QUERY_TRACKER_DYNAMIC_CONFIG") :
        dynconfig = getattr(cls, "QUERY_TRACKER_DYNAMIC_CONFIG")

        config = get("//sys/query_tracker/config")
        config["query_tracker"] = dynconfig
        set("//sys/query_tracker/config", config)


@pytest.fixture
def query_tracker(request, query_tracker_environment):
    cls = request.cls
    count = getattr(cls, "NUM_QUERY_TRACKERS", 1)
    update_query_tracker_environment(cls)

    with QueryTracker(cls.Env, count) as query_tracker:
        yield query_tracker
