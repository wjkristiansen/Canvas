import random

def generate_hex_id():
    value = random.getrandbits(64)
    print(f"0x{value:016X}")

if __name__ == "__main__":
    generate_hex_id()