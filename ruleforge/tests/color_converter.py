from typing import List


def _get_hex() -> str:
    hex_code: str = input("Enter hex code: ")

    if len(hex_code) != 6:
        print("Invalid hex code")
        return _get_hex()

    return hex_code

def convert_to_value(hex_char: str) -> int:
    hex_char = hex_char.upper()

    try:
        int_value = int(hex_char)
        return int_value
    except ValueError:
        if hex_char == "A":
            return 10
        elif hex_char == "B":
            return 11
        elif hex_char == "C":
            return 12
        elif hex_char == "D":
            return 13
        elif hex_char == "E":
            return 14
        elif hex_char == "F":
            return 15

    raise ValueError("Invalid hex code")

def rerun() -> bool:
    again = input("Would you like to rerun the programme? (y/n): ")

    if again == "y":
        return True
    elif again == "n":
        return False

    print("Invalid input")
    return rerun()

def main():
    hex_code = _get_hex()

    rgb: List[int] = [-1, -1, -1]
    rgb[0] = convert_to_value(hex_code[0]) * 16
    rgb[0] += convert_to_value(hex_code[1])
    rgb[1] = convert_to_value(hex_code[2]) * 16
    rgb[1] += convert_to_value(hex_code[3])
    rgb[2] = convert_to_value(hex_code[4]) * 16
    rgb[2] += convert_to_value(hex_code[5])

    print(f"RGB: ({rgb[0]}, {rgb[1]}, {rgb[2]})")

    if rerun():
        main()

if __name__ == "__main__":
    main()
