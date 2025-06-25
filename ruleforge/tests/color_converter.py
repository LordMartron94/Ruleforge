import re
from typing import Tuple, Optional

def hex_to_rgba(hex_code: str) -> Tuple[int, int, int, int]:
    """
    Convert a 6- or 8-digit hex code to RGBA tuple.
    Supports formats with or without '#' prefix.
    """
    hex_code = hex_code.strip().lstrip('#')
    if not re.fullmatch(r"[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?", hex_code):
        raise ValueError("Hex code must be 6 or 8 hexadecimal digits.")
    r = int(hex_code[0:2], 16)
    g = int(hex_code[2:4], 16)
    b = int(hex_code[4:6], 16)
    a = int(hex_code[6:8], 16) if len(hex_code) == 8 else 255
    return r, g, b, a

def rgba_to_hex(r: int, g: int, b: int, a: Optional[int] = None) -> str:
    """
    Convert RGB or RGBA values to hex string.
    r, g, b: 0-255 integers
    a: optional alpha 0-255; if provided, returns 8-digit hex, else 6-digit.
    """
    for name, val in zip(('R', 'G', 'B'), (r, g, b)):
        if not (0 <= val <= 255):
            raise ValueError(f"{name} value must be between 0 and 255.")
    if a is not None and not (0 <= a <= 255):
        raise ValueError("Alpha value must be between 0 and 255.")
    hex_str = f"#{r:02X}{g:02X}{b:02X}"
    if a is not None:
        hex_str += f"{a:02X}"
    return hex_str

def prompt_hex_input() -> str:
    while True:
        user_input = input("Enter hex code (6 or 8 digits, with or without '#'): ").strip()
        try:
            # Validate via conversion
            _ = hex_to_rgba(user_input)
            return user_input
        except ValueError as e:
            print(f"Error: {e}")

def prompt_rgba_input() -> Tuple[int, int, int, Optional[int]]:
    def prompt_channel(name: str, allow_empty: bool = False) -> Optional[int]:
        while True:
            raw = input(f"Enter {name} (0-255){' or leave empty for default 255' if allow_empty else ''}: ").strip()
            if allow_empty and raw == '':
                return 255
            if raw.isdigit():
                val = int(raw)
                if 0 <= val <= 255:
                    return val
            print(f"Invalid {name} value. Please enter an integer between 0 and 255.")

    r = prompt_channel('Red')
    g = prompt_channel('Green')
    b = prompt_channel('Blue')
    a = prompt_channel('Alpha', allow_empty=True)
    return r, g, b, a

def main():
    print("RGBA-HEX Converter")
    while True:
        print("\nChoose an option:")
        print("1) Hex -> RGBA")
        print("2) RGBA -> Hex")
        print("3) Exit")
        choice = input("Your choice: ").strip()
        if choice == '1':
            try:
                hex_code = prompt_hex_input()
                r, g, b, a = hex_to_rgba(hex_code)
                print(f"RGBA: ({r}, {g}, {b}, {a})")
            except ValueError as e:
                print(f"Conversion error: {e}")
        elif choice == '2':
            try:
                r, g, b, a = prompt_rgba_input()
                # If user left alpha empty, function returned 255
                hex_str = rgba_to_hex(r, g, b, a)
                print(f"Hex: {hex_str}")
            except ValueError as e:
                print(f"Conversion error: {e}")
        elif choice == '3':
            print("Goodbye!")
            break
        else:
            print("Invalid option, please choose 1, 2, or 3.")

if __name__ == '__main__':
    main()
