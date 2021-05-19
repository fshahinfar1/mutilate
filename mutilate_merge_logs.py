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
    notice()
    argv = sys.argv
    script_dir = os.path.basedir(argv[0])
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

    cmd = [os.path.join(script_dir, 'mutilate')]
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
    cmd = 'sort -k2 -n {base}/result.txt > {base}/sres.txt'.format(base=base)
    shell(cmd)
    print('Done')


def notice():
    print("\tIf using multi-node trace gathering then make sure\n"
            "\tthe clock of machines are in sync")


if __name__ == '__main__':
    main()
