import re
import sys

def parse_log(filename):
    with open(filename, 'r') as f:
        data = f.read()
    
    dumps = re.findall(r'--- STATE DUMP ---\n(Pod 0:.*?)\n(Pod 1:.*?)\n(Pod 2:.*?)\n(Pod 3:.*?)\n', data)
    for i, dump in enumerate(dumps):
        print(f"Turn {i+1}:")
        for p in dump:
            print(p)
        print()

parse_log('noobkins.log')
