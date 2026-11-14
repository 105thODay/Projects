//! Strict parser for the XML subset CoT uses: one declaration, elements, attributes, the
//! five predefined entities, ASCII character references and comments. Character data is
//! ignored. Errors carry the C++ reference's text and byte offset.

use crate::clib;

/// One element: name, attributes in document order, child elements.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct XmlElement {
    pub name: Vec<u8>,
    pub attributes: Vec<(Vec<u8>, Vec<u8>)>,
    pub children: Vec<XmlElement>,
}

impl XmlElement {
    /// The value of the named attribute, if present.
    pub fn attribute(&self, name: &[u8]) -> Option<&[u8]> {
        self.attributes
            .iter()
            .find(|(key, _)| key.as_slice() == name)
            .map(|(_, value)| value.as_slice())
    }
}

struct Cursor<'a> {
    text: &'a [u8],
    pos: usize,
}

type Parsed<T> = Result<T, Vec<u8>>;

fn find(text: &[u8], from: usize, needle: &[u8]) -> Option<usize> {
    if from > text.len() {
        return None;
    }
    text[from..]
        .windows(needle.len())
        .position(|window| window == needle)
        .map(|offset| from + offset)
}

fn find_byte(text: &[u8], from: usize, byte: u8) -> Option<usize> {
    text.get(from..)?
        .iter()
        .position(|&b| b == byte)
        .map(|offset| from + offset)
}

fn is_name_byte(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b':' | b'-' | b'.')
}

impl Cursor<'_> {
    fn fail<T>(&self, what: &str) -> Parsed<T> {
        Err(format!("{what} at byte {}", self.pos).into_bytes())
    }

    fn skip_space(&mut self) {
        while self.pos < self.text.len()
            && matches!(self.text[self.pos], b' ' | b'\t' | b'\r' | b'\n')
        {
            self.pos += 1;
        }
    }

    fn starts_with(&self, prefix: &[u8]) -> bool {
        self.text[self.pos..].starts_with(prefix)
    }

    fn skip_misc(&mut self) -> Parsed<()> {
        loop {
            self.skip_space();
            if !self.starts_with(b"<!--") {
                return Ok(());
            }
            match find(self.text, self.pos + 4, b"-->") {
                Some(end) => self.pos = end + 3,
                None => return self.fail("unterminated comment"),
            }
        }
    }

    fn name(&mut self) -> Parsed<Vec<u8>> {
        let start = self.pos;
        while self.pos < self.text.len() && is_name_byte(self.text[self.pos]) {
            self.pos += 1;
        }
        if self.pos == start {
            return self.fail("expected a name");
        }
        Ok(self.text[start..self.pos].to_vec())
    }

    fn unescape(&self, raw: &[u8]) -> Parsed<Vec<u8>> {
        let mut out = Vec::with_capacity(raw.len());
        let mut index = 0;
        while index < raw.len() {
            if raw[index] != b'&' {
                out.push(raw[index]);
                index += 1;
                continue;
            }
            let Some(semi) = find_byte(raw, index, b';') else {
                return self.fail("unterminated entity");
            };
            let entity = &raw[index + 1..semi];
            match entity {
                b"amp" => out.push(b'&'),
                b"lt" => out.push(b'<'),
                b"gt" => out.push(b'>'),
                b"quot" => out.push(b'"'),
                b"apos" => out.push(b'\''),
                [b'#', rest @ ..] => {
                    let hex = matches!(rest.first(), Some(b'x' | b'X'));
                    let digits = if hex { &rest[1..] } else { rest };
                    match parse_integer(digits, if hex { 16 } else { 10 }) {
                        Some(code) if (1..=0x7F).contains(&code) => out.push(code as u8),
                        _ => return self.fail("unsupported character reference"),
                    }
                }
                _ => return self.fail("unknown entity"),
            }
            index = semi + 1;
        }
        Ok(out)
    }

    fn element(&mut self) -> Parsed<XmlElement> {
        let mut element = XmlElement::default();
        if self.pos >= self.text.len() || self.text[self.pos] != b'<' {
            return self.fail("expected '<'");
        }
        self.pos += 1;
        element.name = self.name()?;
        loop {
            self.skip_space();
            if self.starts_with(b"/>") {
                self.pos += 2;
                return Ok(element);
            }
            if self.starts_with(b">") {
                self.pos += 1;
                break;
            }
            let name = self.name()?;
            self.skip_space();
            if !self.starts_with(b"=") {
                return self.fail("expected '='");
            }
            self.pos += 1;
            self.skip_space();
            if self.pos >= self.text.len() || !matches!(self.text[self.pos], b'"' | b'\'') {
                return self.fail("expected a quoted value");
            }
            let quote = self.text[self.pos];
            self.pos += 1;
            let Some(end) = find_byte(self.text, self.pos, quote) else {
                return self.fail("unterminated value");
            };
            let value = self.unescape(&self.text[self.pos..end])?;
            if element
                .attributes
                .iter()
                .any(|(existing, _)| *existing == name)
            {
                return self.fail("duplicate attribute");
            }
            element.attributes.push((name, value));
            self.pos = end + 1;
        }
        loop {
            let Some(next) = find_byte(self.text, self.pos, b'<') else {
                return self.fail("unterminated element");
            };
            self.pos = next; // character data is ignored
            if self.starts_with(b"<!--") {
                self.skip_misc()?;
                continue;
            }
            if self.starts_with(b"</") {
                self.pos += 2;
                let name = self.name()?;
                if name != element.name {
                    return self.fail("mismatched end tag");
                }
                self.skip_space();
                if !self.starts_with(b">") {
                    return self.fail("expected '>'");
                }
                self.pos += 1;
                return Ok(element);
            }
            let child = self.element()?;
            element.children.push(child);
        }
    }
}

/// The C++ `ParseInteger`: rejects empty text, then accepts when `strtoll` stops at the
/// end of the C string (an embedded NUL counts as the end).
pub fn parse_integer(text: &[u8], base: u32) -> Option<i64> {
    if text.is_empty() {
        return None;
    }
    let (value, end) = clib::strtoll(text, base);
    (at_c_end(text, end)).then_some(value)
}

/// The C++ `ParseUnsigned`, the `strtoull` counterpart of [`parse_integer`].
pub fn parse_unsigned(text: &[u8], base: u32) -> Option<u64> {
    if text.is_empty() {
        return None;
    }
    let (value, end) = clib::strtoull(text, base);
    (at_c_end(text, end)).then_some(value)
}

/// Whether byte `end` of `text` is where a C string ends: past the bytes or at a NUL.
pub fn at_c_end(text: &[u8], end: usize) -> bool {
    text.get(end).copied().unwrap_or(0) == 0
}

/// Parses `text` as one root element with an optional leading declaration.
pub fn parse_xml(text: &[u8]) -> Result<XmlElement, Vec<u8>> {
    let mut cursor = Cursor { text, pos: 0 };
    if cursor.starts_with(b"<?xml") {
        match find(text, 0, b"?>") {
            Some(end) => cursor.pos = end + 2,
            None => return cursor.fail("unterminated declaration"),
        }
    }
    cursor.skip_misc()?;
    let root = cursor.element()?;
    cursor.skip_misc()?;
    if cursor.pos != text.len() {
        return cursor.fail("content after the root element");
    }
    Ok(root)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_the_subset() {
        let root = parse_xml(b"<?xml version=\"1.0\"?>\n<!-- c --><a x='1' y=\"&lt;&#65;&#x42;\"> text <b/><!--z--></a> ").unwrap();
        assert_eq!(root.name, b"a");
        assert_eq!(root.attribute(b"y"), Some(&b"<AB"[..]));
        assert_eq!(root.children.len(), 1);
    }

    #[test]
    fn errors_match_the_reference() {
        assert_eq!(parse_xml(b"").unwrap_err(), b"expected '<' at byte 0");
        assert_eq!(
            parse_xml(b"<a x='1' x='2'/>").unwrap_err(),
            b"duplicate attribute at byte 12"
        );
        assert_eq!(
            parse_xml(b"<a></b>").unwrap_err(),
            b"mismatched end tag at byte 6"
        );
        assert_eq!(
            parse_xml(b"<a y='&#0;'/>").unwrap_err(),
            b"unsupported character reference at byte 6"
        );
        assert_eq!(
            parse_xml(b"<a/>x").unwrap_err(),
            b"content after the root element at byte 4"
        );
    }
}
