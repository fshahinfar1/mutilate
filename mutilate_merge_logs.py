#!/usr/bin/python3
import argparse
import os
from pprint import pprint
import subprocess
import sys
from time import sleep


def shell(cmd):
    print('cmd:', cmd)
    subprocess.check_output(cmd, shell=True)


def main():
    argv = sys.argv
    save_path = ''
    agents = []
    base = './tmp/'
    for i, arg in enumerate(argv):
        if arg.startswith('--save='):
            _,save_path = arg.split('=')
            break
        elif arg == '-a':
            agents.append(argv[i+1])
        elif arg.startswith('--agent='):
            agent = arg.split('=')
            agents.append(agent)

    print('Agents:')
    pprint(agents)
    print('Log file location:', save_path)

    cmd = ['./mutilate']
    cmd += argv[1:]
    print ('CMD:', cmd)
    p = subprocess.run(cmd)
    if p.returncode != 0:
        print('mutilate failed!')
        sys.exit(1)

    # Should I wait?
    sleep(1)

    if not os.path.isdir(base):
        os.mkdir(base)

    ssh_user = os.environ.get('MUTILATE_SSH_USR')
    if ssh_user is None:
        print('MUTILATE_SSH_USR is not defined')
        sys.exit(1)

    # First copy local file
    k = 0
    cmd = 'cp {path} {base}/_{k}.txt'.format(path=save_path, base=base, k=k)
    shell(cmd)
    k += 1
    # Then fetch other agents files
    for agent in agents:
        cmd = 'scp {user}@{agent}:{path} {base}/_{k}.txt'.format(user=ssh_user,
                agent=agent, path=save_path, base=base, k=k)
        shell(cmd)
        k += 1
    # Merge files and sort
    cmd = 'cat {base}/_*.txt > {base}/result.txt'.format(base=base)
    shell(cmd)
    cmd = 'sort {base}/result.txt > {base}/sres.txt'.format(base=base)
    shell(cmd)
    print('Done')


if __name__ == '__main__':
    main()
