import random
import string
import sys

def generate_large_string(size_bytes, keyword, insert_every=1000):
    for i in range(size_bytes):
        if i % insert_every == 0 and i + len(keyword) < size_bytes:
            print(keyword)
        else:
            print(random.choice(string.ascii_lowercase + string.digits))

str_file_size = int(sys.argv[1])
generate_large_string(str_file_size * 1024 * 1024, "helloworld")
