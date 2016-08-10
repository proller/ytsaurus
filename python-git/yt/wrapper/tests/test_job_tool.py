from helpers import TESTS_SANDBOX, TEST_DIR, TESTS_LOCATION

import yt.wrapper.job_runner as job_runner
import yt.yson as yson
from yt.common import makedirp
import yt.wrapper as yt

import os
import subprocess
import tempfile
import shutil
import pytest

class TestJobRunner(object):
    @classmethod
    def setup_class(cls):
        makedirp(TESTS_SANDBOX)

    def _start_job_runner(self, config):
        with tempfile.NamedTemporaryFile(dir=TESTS_SANDBOX, prefix="job_runner", delete=False) as fout:
            yson.dump(config, fout, yson_format="pretty")

        runner_path = job_runner.__file__
        if runner_path.endswith(".pyc"):
            runner_path = runner_path[:-1]

        return subprocess.Popen([runner_path, "--config-path", fout.name], stderr=subprocess.PIPE)

    @pytest.mark.parametrize("use_yamr_descriptors", [True, False])
    def test_job_runner(self, use_yamr_descriptors):
        tmp_dir = tempfile.mkdtemp(dir=TESTS_SANDBOX)
        command_path = os.path.join(tmp_dir, "command")
        input_path = os.path.join(tmp_dir, "input")
        output_path = os.path.join(tmp_dir, "output")
        sandbox_path = os.path.join(tmp_dir, "sandbox")

        makedirp(sandbox_path)
        makedirp(output_path)

        script="""\
import os
import sys

assert os.environ["YT_JOB_ID"] == "a-b-c-d"
assert os.environ["YT_OPERATION_ID"] == "e-f-g-h"
assert os.environ["YT_JOB_INDEX"] == "0"

for line in sys.stdin:
    fd = int(line)
    os.write(fd, "message for " + str(fd))
"""
        with open(os.path.join(sandbox_path, "script.py"), "w") as fout:
            fout.write(script)
        with open(command_path, "w") as fout:
            fout.write("python script.py")
        with open(input_path, "w") as fout:
            if use_yamr_descriptors:
                descriptors = [1, 3, 4, 5, 6]
            else:
                descriptors = [1, 4, 7, 10]
            fout.write("\n".join(map(str, descriptors)))

        runner = self._start_job_runner({
            "command_path": command_path,
            "sandbox_path": sandbox_path,
            "input_path": input_path,
            "output_path": output_path,
            # In YAMR mode descriptors 1 and 3 correspond to the same table
            "output_table_count": len(descriptors) - int(use_yamr_descriptors),
            "use_yamr_descriptors": use_yamr_descriptors,
            "job_id": "a-b-c-d",
            "operation_id": "e-f-g-h"
        })
        runner.wait()

        assert runner.returncode == 0, runner.stderr.read()
        for fd in descriptors:
            with open(os.path.join(output_path, str(fd))) as fin:
                assert fin.read().strip() == "message for " + str(fd)
        if not use_yamr_descriptors:
            assert os.path.exists(os.path.join(output_path, "5"))  # Statistics descriptor

@pytest.mark.usefixtures("yt_env")
class TestJobTool(object):
    JOB_TOOL_BINARY = os.path.join(TESTS_LOCATION, "../yt-job-tool")

    def _check(self, operation_id, yt_env):
        jobs = yt.list("//sys/operations/{0}/jobs".format(operation_id))
        job_path = subprocess.check_output([
            self.JOB_TOOL_BINARY,
            "prepare-job-environment",
            operation_id,
            jobs[0],
            "--job-path",
            os.path.join(yt_env.env.path, "test_job_tool", "job_" + jobs[0]),
            "--proxy",
            yt_env.config["proxy"]["url"]]).strip()

        assert open(os.path.join(job_path, "sandbox", "_test_file")).read().strip() == "stringdata"
        assert "1\t2\n" == open(os.path.join(job_path, "fail_context")).read()

        run_config = os.path.join(job_path, "run_config")
        assert os.path.exists(run_config)
        with open(run_config, "rb") as fin:
            config = yson.load(fin)
        assert config["operation_id"] == operation_id
        assert config["job_id"] == jobs[0]

        proc = subprocess.Popen([self.JOB_TOOL_BINARY, "run-job", job_path], stderr=subprocess.PIPE)
        proc.wait()

        assert proc.returncode != 0
        assert "RuntimeError" in proc.stderr.read()
        assert "RuntimeError" in open(os.path.join(job_path, "output", "2")).read()

        shutil.rmtree(job_path)

    def test_job_tool(self, yt_env):
        if yt.config["api_version"] != "v3":
            pytest.skip()

        def failing_mapper(rec):
            raise RuntimeError("error")
        def failing_reducer(key, recs):
            raise RuntimeError("error")

        table = TEST_DIR + "/table"
        yt.write_table(table, [{"key": "1", "value": "2"}])
        yt.run_sort(table, sort_by=["key"])

        file_ = TEST_DIR + "/_test_file"
        yt.write_file(file_, "stringdata")

        op = yt.run_map(failing_mapper, table, TEST_DIR + "/output", format="yamr",
                        yt_files=[file_], spec={"max_failed_job_count": 1}, sync=False)
        op.wait(check_result=False)
        self._check(op.id, yt_env)

        op = yt.run_reduce(failing_reducer, table, TEST_DIR + "/output", format="yamr",
                           yt_files=[file_], spec={"max_failed_job_count": 1}, sync=False,
                           reduce_by=["key"])
        op.wait(check_result=False)
        self._check(op.id, yt_env)

        op = yt.run_map_reduce(failing_mapper, "cat", table, TEST_DIR + "/output", format="yamr",
                               map_yt_files=[file_], reduce_by=["key"], spec={"max_failed_job_count": 1},
                               sync=False)
        op.wait(check_result=False)
        self._check(op.id, yt_env)

        op = yt.run_map_reduce("cat", failing_reducer, table, TEST_DIR + "/output", format="yamr",
                               reduce_yt_files=[file_], reduce_by=["key"], spec={"max_failed_job_count": 1},
                               sync=False)
        op.wait(check_result=False)
        self._check(op.id, yt_env)
