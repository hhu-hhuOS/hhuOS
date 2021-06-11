/*
 * Copyright (C) 2018-2021 Heinrich-Heine-Universitaet Duesseldorf,
 * Institute of Computer Science, Department Operating Systems
 * Burak Akguel, Christian Gesse, Fabian Ruhland, Filip Krakowski, Michael Schoettner
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <lib/interface.h>
#include <lib/util/stream/ByteArrayOutputStream.h>
#include <lib/util/stream/PrintWriter.h>
#include "lib/util/data/ArrayList.h"
#include "String.h"
#include "lib/util/memory/Address.h"

namespace Util::Memory {

String::String() noexcept {
    len = 0;
    buffer = new char[1];
    buffer[0] = '\0';
}

String::String(char character) noexcept {
    len = 1;
    buffer = new char[2];
    buffer[0] = character;
    buffer[1] = '\0';
}

String::String(const char *string) noexcept {

    auto address = Memory::Address<uint32_t>(string);

    if (string == nullptr) {

        len = 0;

    } else {

        len = address.stringLength();
    }

    buffer = new char[len + 1];

    Memory::Address<uint32_t>(buffer).copyRange(address, len);

    buffer[len] = '\0';
}

String::String(uint8_t *data, uint32_t length) noexcept {

    auto address = Memory::Address<uint32_t>(data, length);

    len = length;

    buffer = new char[len + 1];

    Memory::Address<uint32_t>(buffer).copyRange(address, len);

    buffer[len] = '\0';
}

String::String(const String &other) noexcept {

    len = other.len;

    buffer = new char[len + 1];

    Memory::Address<uint32_t>(buffer).copyRange(Memory::Address<uint32_t>(other.buffer), len);

    buffer[len] = '\0';
}

String::~String() {

    delete[] buffer;
}

uint32_t String::hashCode() const {
    uint32_t hash = 0;

    for (uint32_t i = 0; i < len; i++) {
        hash += buffer[i];
    }

    return hash;
}

String String::substring(uint32_t begin, uint32_t end) const {

    if (begin > end || begin >= len) {
        return "";
    }

    if (end > len) {
        end = len;
    }

    uint32_t length = end - begin;

    char *buffer = new char[length + 1];

    Memory::Address<uint32_t>(buffer).copyRange(Memory::Address<uint32_t>(this->buffer + begin), length);

    String tmp = "";

    tmp.buffer = buffer;

    tmp.len = length;

    return tmp;
}

Util::Data::Array<String> String::split(const String &delimiter, uint32_t limit) const {

    Util::Data::ArrayList<String> result;

    if (len == 0) {
        return result.toArray();
    }

    uint32_t start = 0;

    uint32_t end = indexOf(delimiter);

    String element;

    while (end != UINT32_MAX && result.size() + 1 != limit) {

        element = substring(start, end);

        if (!element.isEmpty()) {
            result.add(element);
        }

        start = end + delimiter.len;

        end = indexOf(delimiter, start);
    }

    element = substring(start, len);

    if (!element.isEmpty()) {
        result.add(element);
    }

    return result.toArray();
}

bool String::isEmpty() const {
    return len == 0;
}

uint32_t String::indexOf(char character, uint32_t start) const {

    for (uint32_t i = start; i < len; i++) {

        if (buffer[i] == character) {

            return i;
        }
    }

    return UINT32_MAX;
}

uint32_t String::indexOf(const String &other, uint32_t start) const {

    uint32_t i = 0;
    uint32_t j = 0;

    for (i = start; i < len; i++) {

        if (buffer[i] == other.buffer[0]) {

            for (j = 1; j < other.len; j++) {

                if (i + j >= len) {
                    return UINT32_MAX;
                }

                if (buffer[i + j] != other.buffer[j]) {
                    break;
                }
            }
        }

        if (j == other.len) {
            return i;
        }
    }

    return UINT32_MAX;

}


String String::remove(const String &string) const {

    uint32_t index = indexOf(string);

    if (index == UINT32_MAX) {
        return buffer;
    }

    return substring(0, index) + substring(index + string.len, len);
}

String String::removeAll(const String &string) const {

    Util::Data::Array<String> tokens = split(string);

    String tmp;

    for (const String &token : tokens) {
        tmp += token;
    }

    return tmp;
}

bool String::beginsWith(const String &string) const {
    return substring(0, string.len) == string;
}

bool String::endsWith(const String &string) const {
    return substring(len - string.len, len) == string;
}


uint32_t String::length() const {
    return len;
}

bool String::operator==(const String &other) const {
    return Memory::Address<uint32_t>(buffer).compareString(Memory::Address<uint32_t>(other.buffer)) == 0;
}

bool String::operator!=(const String &other) const {
    return Memory::Address<uint32_t>(buffer).compareString(Memory::Address<uint32_t>(other.buffer)) != 0;
}

String &String::operator=(const String &other) {

    if (&other == this) {
        return *this;
    }

    delete buffer;

    buffer = nullptr;

    len = other.len;

    buffer = new char[len + 1];

    if (other.buffer != nullptr) {
        Memory::Address<uint32_t>(buffer).copyRange(Memory::Address<uint32_t>(other.buffer), len + 1);
    }

    return *this;
}

String &String::operator+=(const String &other) {

    buffer = static_cast<char*>(reallocateMemory(buffer, len + other.len + 1));

    Memory::Address<uint32_t>(buffer + len).copyRange(Memory::Address<uint32_t>(other.buffer), other.len + 1);

    len += other.len;

    return *this;
}

String operator+(const String &first, const String &second) {

    String tmp = first;

    tmp += second;

    return tmp;
}

String operator+(const String &first, char second) {

    String tmp = first;

    tmp += String(second);

    return tmp;
}

String operator+(const String &first, const char *second) {

    String tmp = first;

    tmp += String(second);

    return tmp;
}

String operator+(char first, const String &second) {

    String tmp = first;

    tmp += second;

    return tmp;
}

String operator+(const char *first, const String &second) {

    String tmp = first;

    tmp += second;

    return tmp;
}

String::operator char *() const {
    return buffer;
}

char String::operator[](uint32_t index) const {
    return buffer[index];
}

char &String::operator[](uint32_t index) {
    return buffer[index];
}

String::operator uint32_t() const {
    uint32_t hash = 0;

    for (uint32_t i = 0; i < len; i++) {
        hash += buffer[i];
    }

    return hash;
}

String String::join(const String &separator, const Util::Data::Array<String> &elements) {

    String tmp = String();

    uint32_t size = elements.length();

    for (uint32_t i = 0; i < size - 1; i++) {
        tmp += elements[i] + separator;
    }

    tmp += elements[size - 1];

    return tmp;
}

String String::toUpperCase() {

    String tmp = *this;

    char c;

    for (uint32_t i = 0; i < len; i++) {

        c = tmp[i];

        if (isAlpha(tmp[i])) {

            if (c <= 'Z') {

                continue;
            }

            tmp[i] = c - CASE_OFFSET;
        }
    }


    return tmp;
}

String String::toLowerCase() {

    String tmp = *this;

    char c;

    for (uint32_t i = 0; i < len; i++) {

        c = tmp[i];

        if (isAlpha(tmp[i])) {

            if (c >= 'a') {

                continue;
            }

            tmp[i] = c + CASE_OFFSET;
        }
    }


    return tmp;
}

bool String::isAlpha(const char c) {

    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

String String::strip() {

    uint32_t startIndex = 0;

    char element;

    for (startIndex = 0; startIndex < len; startIndex++) {

        element = buffer[startIndex];

        if (element != '\t' && element != ' ') {

            break;
        }
    }

    uint32_t endIndex;

    for (endIndex = len - 1; endIndex > startIndex; endIndex--) {

        element = buffer[endIndex];

        if (element != '\t' && element != ' ') {

            break;
        }
    }

    if (startIndex == endIndex) {

        return "";
    }

    return substring(startIndex, endIndex + 1);
}

String String::format(const char *format, ...) {
    va_list args;
    va_start(args, format);

    String tmp = vformat(format, args);

    va_end(args);

    return tmp;
}

String String::vformat(const char *format, va_list args) {
    auto stream = Stream::ByteArrayOutputStream();
    auto writer = Stream::PrintWriter(stream);

    for (uint32_t i = 0; format[i] != '\0'; i++) {
        uint32_t j;
        for (j = 0; format[i + j] != '%' && format[i + j] != '\0'; j++);

        writer.write(format, i, j);

        if (format[i + j] == '%') {
            if (format[i + j + 1] == '0') {
                uint8_t padding = format[i + j + 2] - '0';
                if (padding < 0 || padding > 9) {
                    Util::Exception::throwException(Util::Exception::INVALID_ARGUMENT, "String: Format padding must be between 01 and 09");
                }

                writer.setNumberPadding(padding);
                j += 2;
            }

            char specifier = format[i + ++j];
            switch (specifier) {
                case 'c' :
                    writer << static_cast<char>(va_arg(args, int32_t));
                    break;
                case 'd' :
                    writer << Stream::PrintWriter::dec << va_arg(args, int32_t);
                    break;
                case 'u' :
                    writer << Stream::PrintWriter::dec << va_arg(args, uint32_t);
                    break;
                case 'o':
                    writer << Stream::PrintWriter::oct << va_arg(args, uint32_t);
                    break;
                case 's':
                    writer << va_arg(args, char*);
                    break;
                case 'x':
                    writer << Stream::PrintWriter::hex << va_arg(args, uint32_t);
                    break;
                case 'b':
                    writer << Stream::PrintWriter::bin << va_arg(args, uint32_t);
                    break;
                case 'B':
                    writer << static_cast<bool>(va_arg(args, uint32_t));
                    break;
                default:
                    Exception::throwException(Exception::INVALID_ARGUMENT, "String: Invalid format specifier!");
            }
        } else {
            break;
        }

        writer.setNumberPadding(0);
        i += j;
    }

    return stream.getContent();
}

}