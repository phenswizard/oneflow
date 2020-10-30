import os
import subprocess
import socket
import tempfile
from contextlib import closing
from subprocess import TimeoutExpired
import argparse

FIX_SSH_PERMISSION = """
mkdir -p /run/sshd
chown root ~/.ssh
chmod 700 ~/.ssh
chown root ~/.ssh/*
chmod 600 ~/.ssh/*
chmod 400 ~/.ssh/id_rsa
chmod 400 ~/.ssh/id_rsa.pub
chmod 600 ~/.ssh/config
"""


def find_free_port():
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
        s.bind(("localhost", 0))
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return s.getsockname()[1]


def make_dotssh(dotssh_dir):
    bash_cmd = f"""set -ex
rm -rf /root/.ssh
ssh-keygen -t rsa -N "" -f /root/.ssh/id_rsa
cat /root/.ssh/id_rsa.pub >> /root/.ssh/authorized_keys && \
    chmod 600 /root/.ssh/authorized_keys
/etc/init.d/ssh start && \
    ssh-keyscan -H localhost >> /root/.ssh/known_hosts

rm -rf {dotssh_dir}
cp -r /root/.ssh/ {dotssh_dir}
chmod 777 {dotssh_dir}
chmod 777 {dotssh_dir}/*
"""
    with tempfile.NamedTemporaryFile(mode="w+", encoding="utf-8") as f:
        f_name = f.name
        f.write(bash_cmd)
        f.flush()
        subprocess.check_call(
            f"docker run -v /tmp:/host/tmp -v $PWD:$PWD -w $PWD oneflow-test:$USER bash /host/{f_name}",
            shell=True,
        )
    config_content = """Host *
	StrictHostKeyChecking no
"""
    with open(os.path.join(dotssh_dir, "config"), "w") as f:
        f.write(config_content)


def launch_remote_container(hostname, docker_ssh_port, survival_time, dotssh_dir):
    workspace_name = "distributed_run_workspace"
    subprocess.check_call(
        f"ssh {hostname} docker run --rm -v $HOME:$HOME -w $HOME busybox rm -rf {workspace_name}",
        shell=True,
    )
    subprocess.check_call(f"ssh {hostname} mkdir ~/{workspace_name}/", shell=True)
    subprocess.check_call(
        f"scp -r {dotssh_dir} {hostname}:~/{workspace_name}/dotssh", shell=True
    )
    bash_cmd = f"""set -ex
{FIX_SSH_PERMISSION}
/usr/sbin/sshd -p {docker_ssh_port}
sleep {survival_time}
"""
    with tempfile.NamedTemporaryFile(mode="w+", encoding="utf-8") as f:
        f_name = f.name
        f.write(bash_cmd)
        f.flush()
        subprocess.check_call(
            f"scp {f_name} {hostname}:~/{workspace_name}/launch_ssh_server.sh",
            shell=True,
        )
        home_dir = os.path.expanduser("~")
    docker_cmd = f"""docker run --privileged --network=host --shm-size=8g --rm -v {home_dir}/{workspace_name}/dotssh:/root/.ssh -v {home_dir}/{workspace_name}:/{workspace_name} -w /{workspace_name} -v /dataset:/dataset -v /model_zoo:/model_zoo oneflow-test:$USER bash launch_ssh_server.sh
"""
    ssh_cmd = f"ssh {hostname} {docker_cmd}"
    print(ssh_cmd)
    proc = subprocess.Popen(ssh_cmd, shell=True,)
    try:
        proc.wait(timeout=15)
        raise ValueError("sshd quit early, returncode:", proc.returncode)
    except TimeoutExpired:
        survival_time_min = survival_time / 60
        survival_time_min = int(survival_time_min)
        print(
            f"remote container launched, host: {hostname}, ssh port:{docker_ssh_port}, .ssh dir: {dotssh_dir}, survival: {survival_time_min} mins"
        )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--launch_remote_container", action="store_true", required=False, default=False
    )
    parser.add_argument(
        "--make_dotssh", action="store_true", required=False, default=False
    )
    parser.add_argument("--run", action="store_true", required=False, default=False)
    parser.add_argument("--bash_script", type=str, required=False)
    parser.add_argument("--remote_host", type=str, required=False, default="oneflow-15")
    parser.add_argument("--dotssh_dir", type=str, required=False, default="dotssh")
    args = parser.parse_args()

    ssh_port = find_free_port()
    if args.launch_remote_container:
        launch_remote_container(args.remote_host, ssh_port, 10 * 60, args.dotssh_dir)
        exit(0)

    if args.make_dotssh:
        make_dotssh(args.dotssh_dir)
        exit(0)

    if args.run:
        assert args.bash_script
        run_bash_script(args.run_bash_script)
        exit(0)
