// GPL-3.0-or-later. Copyright (C) 2026 Nuaptan Felix Evalisk.
// Shared Hayagriva engine with an owned UTF-8 C ABI; no files or subprocesses.
use std::ffi::{CString, c_char};
use std::panic::{AssertUnwindSafe, catch_unwind};

use hayagriva::archive::{ArchivedStyle, locales};
use hayagriva::citationberg::taxonomy::Locator;
use hayagriva::citationberg::{
    Display, FontStyle, FontVariant, FontWeight, Style, TextDecoration, VerticalAlign,
};
use hayagriva::{
    BibliographyDriver, BibliographyRequest, BufWriteFormat, CitationItem, CitationRequest, Elem,
    ElemChild, LocatorPayload, SpecificLocator,
};
use serde::{Deserialize, Serialize};

#[derive(Deserialize)]
struct RenderItem {
    key: String,
    #[serde(default)]
    locator_type: String,
    #[serde(default)]
    locator_value: String,
    #[serde(default)]
    hidden: bool,
}

#[derive(Deserialize)]
struct RenderCluster {
    items: Vec<RenderItem>,
}

#[derive(Deserialize)]
struct RenderRequest {
    #[serde(default = "default_style")]
    style: String,
    #[serde(default)]
    citations: Vec<RenderCluster>,
    #[serde(default)]
    bibliography_only: Vec<String>,
}

#[derive(Serialize)]
struct RenderResponse {
    citations: Vec<String>,
    bibliography: Vec<RenderedBibliographyItem>,
}

#[derive(Serialize)]
struct RenderedBibliographyItem {
    key: String,
    html: String,
}

#[derive(Serialize)]
struct StyleSummary {
    name: String,
    title: String,
}

fn default_style() -> String {
    "springer-mathphys".to_owned()
}

pub fn list_styles() -> Result<String, String> {
    let mut styles = ArchivedStyle::all()
        .iter()
        .copied()
        .filter_map(|archived| {
            let style = archived.get();
            if !matches!(&style, Style::Independent(_)) {
                return None;
            }
            let name = archived
                .names()
                .iter()
                .min_by_key(|name| name.chars().count())
                .expect("archived CSL style must have a name");
            Some(StyleSummary {
                name: (*name).to_owned(),
                title: style.info().title.value.clone(),
            })
        })
        .collect::<Vec<_>>();
    styles.sort_by(|left, right| {
        left.title
            .to_lowercase()
            .cmp(&right.title.to_lowercase())
            .then_with(|| left.name.cmp(&right.name))
    });
    serde_json::to_string(&styles).map_err(|error| error.to_string())
}

fn escape_html(text: &str, output: &mut String) {
    for ch in text.chars() {
        match ch {
            '&' => output.push_str("&amp;"),
            '<' => output.push_str("&lt;"),
            '>' => output.push_str("&gt;"),
            '"' => output.push_str("&quot;"),
            '\'' => output.push_str("&#39;"),
            _ => output.push(ch),
        }
    }
}

fn render_formatted(text: &hayagriva::Formatted, output: &mut String) {
    let formatting = text.formatting;
    let mut prefix = String::new();
    let mut suffix = String::new();
    let mut wrap = |start: &str, end: &str| {
        prefix.push_str(start);
        suffix.insert_str(0, end);
    };
    match formatting.vertical_align {
        VerticalAlign::Sub => wrap("<sub>", "</sub>"),
        VerticalAlign::Sup => wrap("<sup>", "</sup>"),
        VerticalAlign::Baseline => wrap("<span style=\"vertical-align:baseline\">", "</span>"),
        VerticalAlign::None => {}
    }
    match formatting.font_weight {
        FontWeight::Bold => wrap("<b>", "</b>"),
        FontWeight::Light => wrap("<span style=\"font-weight:lighter\">", "</span>"),
        FontWeight::Normal => {}
    }
    if formatting.font_style == FontStyle::Italic {
        wrap("<i>", "</i>");
    }
    if formatting.font_variant == FontVariant::SmallCaps {
        wrap("<span style=\"font-variant:small-caps\">", "</span>");
    }
    if formatting.text_decoration == TextDecoration::Underline {
        wrap("<u>", "</u>");
    }
    output.push_str(&prefix);
    escape_html(&text.text, output);
    output.push_str(&suffix);
}

fn render_child(child: &ElemChild, output: &mut String) {
    match child {
        ElemChild::Text(text) => render_formatted(text, output),
        ElemChild::Elem(elem) => render_elem(elem, output),
        ElemChild::Link { text, url } => {
            output.push_str("<a href=\"");
            escape_html(url, output);
            output.push_str("\">");
            render_formatted(text, output);
            output.push_str("</a>");
        }
        other => {
            let _ = other.write_buf(output, BufWriteFormat::Html);
        }
    }
}

fn render_elem(elem: &Elem, output: &mut String) {
    let suffix = match elem.display {
        Some(Display::Block) => {
            output.push_str("<div class=\"csl-block\">");
            "</div>"
        }
        Some(Display::LeftMargin) => {
            output.push_str("<div class=\"csl-left-margin\">");
            "</div>"
        }
        Some(Display::RightInline) => {
            output.push_str("<div class=\"csl-right-inline\">");
            "</div>"
        }
        Some(Display::Indent) => {
            output.push_str("<div class=\"csl-indent\">");
            "</div>"
        }
        None => "",
    };
    for child in &elem.children.0 {
        render_child(child, output);
    }
    output.push_str(suffix);
}

fn render_children(children: &hayagriva::ElemChildren) -> String {
    let mut output = String::new();
    for child in &children.0 {
        render_child(child, &mut output);
    }
    output
}

fn parse_locator(name: &str) -> Locator {
    match name.to_ascii_lowercase().as_str() {
        "page" | "pages" => Locator::Page,
        "chapter" => Locator::Chapter,
        "section" => Locator::Section,
        "paragraph" => Locator::Paragraph,
        "figure" => Locator::Figure,
        "table" => Locator::Table,
        "volume" => Locator::Volume,
        "issue" => Locator::Issue,
        _ => Locator::Custom,
    }
}

pub fn import_bib(source: &str) -> Result<String, String> {
    let library = hayagriva::io::from_biblatex_str(source).map_err(|errors| {
        errors
            .iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
            .join("\n")
    })?;
    serde_json::to_string(&library).map_err(|e| e.to_string())
}

pub fn render(source: &str, request_json: &str) -> Result<String, String> {
    let library = hayagriva::io::from_biblatex_str(source).map_err(|errors| {
        errors
            .iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
            .join("\n")
    })?;
    let request: RenderRequest = serde_json::from_str(request_json).map_err(|e| e.to_string())?;
    let archived = ArchivedStyle::by_name(&request.style)
        .ok_or_else(|| format!("unknown CSL style: {}", request.style))?;
    let Style::Independent(style) = archived.get() else {
        return Err(format!(
            "dependent CSL style is not directly renderable: {}",
            request.style
        ));
    };
    let locale_files = locales();
    let mut driver = BibliographyDriver::new();
    let locator_storage: Vec<Vec<String>> = request
        .citations
        .iter()
        .map(|cluster| {
            cluster
                .items
                .iter()
                .map(|item| item.locator_value.clone())
                .collect()
        })
        .collect();
    for (cluster_index, cluster) in request.citations.iter().enumerate() {
        let mut items = Vec::new();
        for (item_index, item) in cluster.items.iter().enumerate() {
            let entry = library
                .get(&item.key)
                .ok_or_else(|| format!("unknown bibliography key: {}", item.key))?;
            let locator = if item.locator_value.is_empty() {
                None
            } else {
                Some(SpecificLocator(
                    parse_locator(&item.locator_type),
                    LocatorPayload::Str(locator_storage[cluster_index][item_index].as_str()),
                ))
            };
            items.push(CitationItem::new(entry, locator, None, item.hidden, None));
        }
        driver.citation(CitationRequest::new(
            items,
            &style,
            None,
            &locale_files,
            None,
        ));
    }
    if !request.bibliography_only.is_empty() {
        let mut items = Vec::new();
        for key in &request.bibliography_only {
            let entry = library
                .get(key)
                .ok_or_else(|| format!("unknown bibliography key: {key}"))?;
            items.push(CitationItem::new(entry, None, None, true, None));
        }
        driver.citation(CitationRequest::new(
            items,
            &style,
            None,
            &locale_files,
            None,
        ));
    }
    let rendered = driver.finish(BibliographyRequest::new(&style, None, &locale_files));
    let citations = rendered
        .citations
        .iter()
        .map(|citation| render_children(&citation.citation))
        .collect();
    let bibliography = rendered
        .bibliography
        .map(|bibliography| {
            bibliography
                .items
                .iter()
                .map(|item| {
                    let mut output = String::new();
                    if let Some(first) = &item.first_field {
                        render_child(first, &mut output);
                    }
                    output.push_str(&render_children(&item.content));
                    RenderedBibliographyItem {
                        key: item.key.clone(),
                        html: output,
                    }
                })
                .collect()
        })
        .unwrap_or_default();
    serde_json::to_string(&RenderResponse {
        citations,
        bibliography,
    })
    .map_err(|e| e.to_string())
}

unsafe fn input<'a>(data: *const u8, len: usize) -> Result<&'a str, String> {
    if len == 0 {
        return Ok("");
    }
    if data.is_null() {
        return Err("null Materials input".into());
    }
    std::str::from_utf8(unsafe { std::slice::from_raw_parts(data, len) })
        .map_err(|error| error.to_string())
}

/// Callers supply valid spans and two distinct writable output pointers.
/// Returned strings must be released with athena_materials_free.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn athena_materials_call(
    operation: u32,
    source: *const u8,
    source_len: usize,
    request: *const u8,
    request_len: usize,
    output: *mut *mut c_char,
    error: *mut *mut c_char,
) -> i32 {
    if output.is_null() || error.is_null() || output == error {
        return -1;
    }
    unsafe {
        *output = std::ptr::null_mut();
        *error = std::ptr::null_mut();
    }
    let result = catch_unwind(AssertUnwindSafe(|| match operation {
        0 => list_styles(),
        1 => import_bib(unsafe { input(source, source_len)? }),
        2 => render(unsafe { input(source, source_len)? }, unsafe {
            input(request, request_len)?
        }),
        _ => Err("unknown Materials operation".into()),
    }))
    .unwrap_or_else(|_| Err("Materials engine panicked".into()));
    match result {
        Ok(value) => match CString::new(value) {
            Ok(value) => {
                unsafe {
                    *output = value.into_raw();
                }
                0
            }
            Err(_) => {
                unsafe {
                    *error = CString::new("Materials output contains NUL")
                        .unwrap()
                        .into_raw();
                }
                1
            }
        },
        Err(value) => {
            let value = CString::new(value.replace('\0', "\\0")).unwrap();
            unsafe {
                *error = value.into_raw();
            }
            1
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn athena_materials_free(value: *mut c_char) {
    if !value.is_null() {
        drop(unsafe { CString::from_raw(value) });
    }
}
