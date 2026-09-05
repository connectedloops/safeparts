use std::fs;
use std::path::PathBuf;
use std::time::{Duration, Instant};

use anyhow::{Context, Result};
use base64::Engine;
use crossterm::event::{Event, KeyCode, KeyEvent, KeyModifiers};
use ratatui::layout::{Alignment, Constraint, Direction, Layout, Margin, Rect};
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span, Text};
use ratatui::widgets::{
    Block, Borders, Cell, Clear, List, ListItem, Paragraph, Row, Table, Tabs, Wrap,
};
use ratatui::{Frame, Terminal};
use tui_textarea::{Input, TextArea};
use zeroize::{Zeroize, Zeroizing};

use crate::clipboard::Clipboard;
use crate::domain::{Encoding, combine_shares, set_id_hex, split_secret};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum TabId {
    Split,
    Combine,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ModalKind {
    LoadSecretFile,
    LoadShareFiles,
    SaveSharesDir,
    SaveSecretFile,
}

#[derive(Debug)]
struct Modal {
    kind: ModalKind,
    input: TextArea<'static>,
}

impl Modal {
    fn new(kind: ModalKind, placeholder: &str) -> Self {
        let mut input = TextArea::default();
        input.set_placeholder_text(placeholder);
        Self { kind, input }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Focus {
    SplitSecret,
    SplitK,
    SplitN,
    SplitEncoding,
    SplitPassphrase,
    SplitShares,

    CombineShares,
    CombineEncoding,
    CombinePassphrase,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum StatusKind {
    Info,
    Ok,
    Error,
}

#[derive(Clone, Debug)]
struct Status {
    kind: StatusKind,
    msg: String,
    at: Instant,
}

#[derive(Clone, Copy, Debug)]
struct Theme {
    accent: Color,
    dim: Color,
    ok: Color,
    err: Color,
    border: Color,
}

impl Default for Theme {
    fn default() -> Self {
        Self {
            accent: Color::Cyan,
            dim: Color::Gray,
            ok: Color::Green,
            err: Color::Red,
            border: Color::DarkGray,
        }
    }
}

pub struct App {
    tab: TabId,
    focus: Focus,

    // split
    split_secret_text: TextArea<'static>,
    split_secret_file: Option<PathBuf>,
    split_secret_file_len: Option<usize>,
    split_k: u8,
    split_n: u8,
    split_encoding: Encoding,
    split_passphrase: Zeroizing<String>,
    split_shares: Vec<String>,
    split_packets: Vec<safeparts_core::packet::SharePacket>,
    split_selected_share: usize,

    // combine
    combine_shares_text: TextArea<'static>,
    combine_encoding: Encoding,
    combine_passphrase: Zeroizing<String>,
    combine_recovered: Option<Zeroizing<Vec<u8>>>,
    combine_recovered_text: Option<Zeroizing<String>>,
    combine_used_encoding: Option<Encoding>,

    // common
    clipboard: Clipboard,
    status: Option<Status>,
    show_help: bool,
    modal: Option<Modal>,
    theme: Theme,
}

impl App {
    pub fn new() -> Self {
        let theme = Theme::default();

        let mut split_secret_text = TextArea::default();
        split_secret_text
            .set_placeholder_text("Paste secret text here (UTF-8) or Ctrl+L to load a file...");

        let combine_shares_text = Self::new_combine_shares_text();

        Self {
            tab: TabId::Split,
            focus: Focus::SplitSecret,

            split_secret_text,
            split_secret_file: None,
            split_secret_file_len: None,
            split_k: 2,
            split_n: 3,
            split_encoding: Encoding::Base64url,
            split_passphrase: Zeroizing::new(String::new()),
            split_shares: Vec::new(),
            split_packets: Vec::new(),
            split_selected_share: 0,

            combine_shares_text,
            combine_encoding: Encoding::Auto,
            combine_passphrase: Zeroizing::new(String::new()),
            combine_recovered: None,
            combine_recovered_text: None,
            combine_used_encoding: None,

            clipboard: Clipboard::new(),
            status: None,
            show_help: false,
            modal: None,
            theme,
        }
    }

    fn new_combine_shares_text() -> TextArea<'static> {
        let mut combine_shares_text = TextArea::default();
        combine_shares_text.set_placeholder_text(
            "Paste shares here.\n\n- base64url/base58check: whitespace-separated\n- mnemonics: one share per paragraph (blank-line separated)",
        );
        combine_shares_text
    }

    pub fn run<B: ratatui::backend::Backend>(mut self, terminal: &mut Terminal<B>) -> Result<()> {
        loop {
            terminal
                .draw(|f| self.render(f))
                .map_err(|error| anyhow::anyhow!("draw terminal: {error}"))?;

            if crossterm::event::poll(Duration::from_millis(50))? {
                match crossterm::event::read()? {
                    Event::Key(key) if self.on_key(key)? => {
                        break;
                    }
                    Event::Key(_) | Event::Resize(_, _) => {}
                    _ => {}
                }
            }

            self.expire_status();
        }

        Ok(())
    }

    fn expire_status(&mut self) {
        if self
            .status
            .as_ref()
            .is_some_and(|status| status.at.elapsed() > Duration::from_secs(3))
        {
            self.status = None;
        }
    }

    fn set_status(&mut self, kind: StatusKind, msg: impl Into<String>) {
        self.status = Some(Status {
            kind,
            msg: msg.into(),
            at: Instant::now(),
        });
    }

    fn set_ok(&mut self, msg: impl Into<String>) {
        self.set_status(StatusKind::Ok, msg);
    }

    fn set_err(&mut self, msg: impl Into<String>) {
        self.set_status(StatusKind::Error, msg);
    }

    fn set_info(&mut self, msg: impl Into<String>) {
        self.set_status(StatusKind::Info, msg);
    }

    fn on_key(&mut self, key: KeyEvent) -> Result<bool> {
        if self.show_help {
            if key.code == KeyCode::Esc || key.code == KeyCode::Char('?') {
                self.show_help = false;
            }
            return Ok(false);
        }

        if self.modal.is_some() {
            return self.on_modal_key(key);
        }

        if is_control_key(key, 'q') {
            return Ok(true);
        }

        if key.code == KeyCode::F(1) || key.code == KeyCode::Char('?') {
            self.show_help = true;
            return Ok(false);
        }

        if key.code == KeyCode::Tab {
            self.next_focus();
            return Ok(false);
        }

        if key.code == KeyCode::BackTab {
            self.prev_focus();
            return Ok(false);
        }

        if is_control_key(key, 'l') {
            self.on_load();
            return Ok(false);
        }

        if is_control_key(key, 's') {
            self.on_save();
            return Ok(false);
        }

        if is_control_key(key, 'c') {
            self.on_copy()?;
            return Ok(false);
        }

        if is_control_key(key, 'v') {
            self.on_paste()?;
            return Ok(false);
        }

        match key.code {
            KeyCode::Left => {
                self.prev_tab();
                Ok(false)
            }
            KeyCode::Right => {
                self.next_tab();
                Ok(false)
            }
            KeyCode::Enter => {
                self.on_enter()?;
                Ok(false)
            }
            KeyCode::Up => {
                self.on_up();
                Ok(false)
            }
            KeyCode::Down => {
                self.on_down();
                Ok(false)
            }
            _ => {
                self.forward_to_widget(key);
                Ok(false)
            }
        }
    }

    fn invalidate_split(&mut self) {
        self.split_shares.zeroize();
        for packet in &mut self.split_packets {
            packet.payload.zeroize();
        }
        self.split_packets.clear();
        self.split_selected_share = 0;
        self.status = None;
    }

    fn invalidate_combine(&mut self) {
        self.combine_recovered = None;
        self.combine_recovered_text = None;
        self.combine_used_encoding = None;
        self.status = None;
    }

    fn edit_text(
        textarea: &mut TextArea<'static>,
        edit: impl FnOnce(&mut TextArea<'static>) -> bool,
    ) -> bool {
        // Widget edit flags include replacing a selection with identical text.
        let previous = Zeroizing::new(textarea.lines().to_vec());
        edit(textarea) && previous.as_slice() != textarea.lines()
    }

    fn forward_to_widget(&mut self, key: KeyEvent) {
        match self.focus {
            Focus::SplitSecret => {
                let input: Input = key.into();
                if Self::edit_text(&mut self.split_secret_text, |text| text.input(input)) {
                    self.split_secret_file = None;
                    self.split_secret_file_len = None;
                    self.invalidate_split();
                }
            }
            Focus::CombineShares => {
                let input: Input = key.into();
                if Self::edit_text(&mut self.combine_shares_text, |text| text.input(input)) {
                    self.invalidate_combine();
                }
            }
            Focus::SplitPassphrase => {
                if Self::passphrase_input(key, &mut self.split_passphrase) {
                    self.invalidate_split();
                }
            }
            Focus::CombinePassphrase => {
                if Self::passphrase_input(key, &mut self.combine_passphrase) {
                    self.invalidate_combine();
                }
            }
            _ => {}
        }
    }

    fn passphrase_input(key: KeyEvent, buf: &mut Zeroizing<String>) -> bool {
        let old_len = buf.len();
        if is_control_key(key, 'u') {
            buf.clear();
            return old_len != 0;
        }

        match key.code {
            KeyCode::Backspace => {
                buf.pop();
            }
            KeyCode::Char(c)
                if !key.modifiers.contains(KeyModifiers::CONTROL)
                    && !key.modifiers.contains(KeyModifiers::ALT) =>
            {
                buf.push(c);
            }
            _ => {}
        }
        buf.len() != old_len
    }

    fn on_up(&mut self) {
        let previous = (self.split_k, self.split_n, self.split_encoding);
        match self.focus {
            Focus::SplitShares => {
                self.split_selected_share = self.split_selected_share.saturating_sub(1);
            }
            Focus::SplitK => {
                self.split_k = self.split_k.saturating_add(1).min(self.split_n).max(1);
            }
            Focus::SplitN => {
                self.split_n = self.split_n.saturating_add(1);
                self.split_k = self.split_k.min(self.split_n);
            }
            Focus::SplitEncoding => {
                self.split_encoding = cycle_encoding(self.split_encoding, -1, Encoding::SPLIT);
            }
            Focus::CombineEncoding => {
                self.combine_encoding = cycle_encoding(self.combine_encoding, -1, Encoding::ALL);
                self.invalidate_combine();
            }
            _ => {}
        }
        if previous != (self.split_k, self.split_n, self.split_encoding) {
            self.invalidate_split();
        }
    }

    fn on_down(&mut self) {
        let previous = (self.split_k, self.split_n, self.split_encoding);
        match self.focus {
            Focus::SplitShares if !self.split_shares.is_empty() => {
                self.split_selected_share =
                    (self.split_selected_share + 1).min(self.split_shares.len() - 1);
            }
            Focus::SplitShares => {}
            Focus::SplitK => {
                self.split_k = self.split_k.saturating_sub(1).max(1);
            }
            Focus::SplitN => {
                self.split_n = self.split_n.saturating_sub(1).max(1);
                self.split_k = self.split_k.min(self.split_n);
            }
            Focus::SplitEncoding => {
                self.split_encoding = cycle_encoding(self.split_encoding, 1, Encoding::SPLIT);
            }
            Focus::CombineEncoding => {
                self.combine_encoding = cycle_encoding(self.combine_encoding, 1, Encoding::ALL);
                self.invalidate_combine();
            }
            _ => {}
        }
        if previous != (self.split_k, self.split_n, self.split_encoding) {
            self.invalidate_split();
        }
    }

    fn on_enter(&mut self) -> Result<()> {
        match self.tab {
            TabId::Split => self.do_split(),
            TabId::Combine => self.do_combine(),
        }
    }

    fn on_load(&mut self) {
        self.modal = Some(match self.tab {
            TabId::Split => Modal::new(ModalKind::LoadSecretFile, "Enter secret file path (bytes)"),
            TabId::Combine => Modal::new(
                ModalKind::LoadShareFiles,
                "Enter share file paths (one per line)",
            ),
        });
    }

    fn on_save(&mut self) {
        let modal = match self.tab {
            TabId::Split if self.split_shares.is_empty() => {
                self.set_info("no shares to save");
                return;
            }
            TabId::Split => Modal::new(
                ModalKind::SaveSharesDir,
                "Enter output directory for share files",
            ),
            TabId::Combine if self.combine_recovered.is_none() => {
                self.set_info("nothing to save");
                return;
            }
            TabId::Combine => Modal::new(
                ModalKind::SaveSecretFile,
                "Enter output file path for recovered secret",
            ),
        };
        self.modal = Some(modal);
    }

    fn on_copy(&mut self) -> Result<()> {
        let text = match self.tab {
            TabId::Split => {
                if self.split_shares.is_empty() {
                    self.set_info("no shares to copy");
                    return Ok(());
                }

                if self.focus != Focus::SplitShares {
                    self.set_info("focus Recovery shares to copy one");
                    return Ok(());
                }

                self.split_shares
                    .get(self.split_selected_share)
                    .cloned()
                    .unwrap_or_default()
            }
            TabId::Combine => {
                if let Some(text) = self.combine_recovered_text.as_ref() {
                    text.as_str().to_string()
                } else if let Some(bytes) = self.combine_recovered.as_ref() {
                    base64::engine::general_purpose::STANDARD.encode(bytes.as_slice())
                } else {
                    self.set_info("nothing to copy");
                    return Ok(());
                }
            }
        };

        match self.clipboard.set_text(&text) {
            Ok(crate::clipboard::CopyMethod::System) => self.set_ok("copied to clipboard"),
            Ok(crate::clipboard::CopyMethod::Osc52) => {
                self.set_info("copied via OSC52; clipboard may be shared")
            }
            Err(e) => self.set_err(format!("copy failed: {e}")),
        }
        Ok(())
    }

    fn on_paste(&mut self) -> Result<()> {
        let Ok(text) = self.clipboard.get_text() else {
            self.set_info("paste unavailable");
            return Ok(());
        };

        match self.focus {
            Focus::SplitSecret => {
                if Self::edit_text(&mut self.split_secret_text, |field| field.insert_str(text)) {
                    self.split_secret_file = None;
                    self.split_secret_file_len = None;
                    self.invalidate_split();
                }
            }
            Focus::CombineShares => {
                if Self::edit_text(&mut self.combine_shares_text, |field| {
                    field.insert_str(text)
                }) {
                    self.invalidate_combine();
                }
            }
            Focus::SplitPassphrase => {
                if !text.is_empty() {
                    self.split_passphrase.push_str(&text);
                    self.invalidate_split();
                }
            }
            Focus::CombinePassphrase => {
                if !text.is_empty() {
                    self.combine_passphrase.push_str(&text);
                    self.invalidate_combine();
                }
            }
            _ => self.set_info("paste into a text field"),
        }

        Ok(())
    }

    fn on_modal_key(&mut self, key: KeyEvent) -> Result<bool> {
        if key.code == KeyCode::Esc {
            self.modal = None;
            return Ok(false);
        }

        let Some(modal) = self.modal.as_mut() else {
            return Ok(false);
        };

        if key.code == KeyCode::Enter {
            let text = modal.input.lines().join("\n").trim().to_string();

            let kind = modal.kind;
            self.modal = None;
            self.apply_modal(kind, text)?;
            return Ok(false);
        }

        modal.input.input(key);
        Ok(false)
    }

    fn apply_modal(&mut self, kind: ModalKind, text: String) -> Result<()> {
        match kind {
            ModalKind::LoadSecretFile => {
                let p = PathBuf::from(text);
                let bytes = fs::read(&p).with_context(|| format!("read {}", p.display()))?;
                self.invalidate_split();
                self.split_secret_file_len = Some(bytes.len());
                self.split_secret_file = Some(p);

                self.split_secret_text = TextArea::default();
                self.split_secret_text.set_placeholder_text(
                    "Using secret file input. Type here to switch back to text input.",
                );

                self.set_ok("loaded secret file");
            }
            ModalKind::LoadShareFiles => {
                let mut combined = String::new();
                for line in text.lines().map(str::trim).filter(|l| !l.is_empty()) {
                    let p = PathBuf::from(line);
                    let s =
                        fs::read_to_string(&p).with_context(|| format!("read {}", p.display()))?;
                    combined.push_str(s.trim());
                    combined.push_str("\n\n");
                }
                if self.combine_shares_text.lines().join("\n") != combined {
                    self.invalidate_combine();
                }
                self.combine_shares_text = Self::new_combine_shares_text();
                self.combine_shares_text.insert_str(combined);
                self.set_ok("loaded share files");
            }
            ModalKind::SaveSharesDir => {
                let dir = if text.is_empty() {
                    PathBuf::from(".")
                } else {
                    PathBuf::from(text)
                };
                fs::create_dir_all(&dir)
                    .with_context(|| format!("create dir {}", dir.display()))?;

                let set_id = set_id_hex(&self.split_packets).unwrap_or_else(|| "unknown".into());
                let n = self.split_shares.len();

                for (idx, share) in self.split_shares.iter().enumerate() {
                    let i = idx + 1;
                    let filename = format!("safeparts-{set_id}-share-{i}-of-{n}.txt");
                    let path = dir.join(filename);
                    crate::private_file::write(&path, format!("{share}\n").as_bytes())?;
                }

                self.set_ok(format!("saved {n} share files"));
            }
            ModalKind::SaveSecretFile => {
                let Some(bytes) = self.combine_recovered.as_ref() else {
                    self.set_info("nothing to save");
                    return Ok(());
                };

                if text.is_empty() {
                    self.set_info("output path required");
                    return Ok(());
                }

                let path = PathBuf::from(text);
                crate::private_file::write(&path, bytes.as_slice())?;
                self.set_ok("saved recovered secret");
            }
        }

        Ok(())
    }

    fn do_split(&mut self) -> Result<()> {
        self.invalidate_split();
        let secret_bytes = if let Some(path) = self.split_secret_file.as_ref() {
            fs::read(path).with_context(|| format!("read {}", path.display()))?
        } else {
            self.split_secret_text.lines().join("\n").into_bytes()
        };
        let secret_bytes = Zeroizing::new(secret_bytes);

        if secret_bytes.is_empty() {
            self.set_info("secret is empty");
            return Ok(());
        }

        let passphrase = if self.split_passphrase.is_empty() {
            None
        } else {
            Some(self.split_passphrase.as_bytes())
        };

        match split_secret(
            secret_bytes.as_slice(),
            self.split_k,
            self.split_n,
            self.split_encoding,
            passphrase,
        ) {
            Ok((packets, shares)) => {
                self.split_packets = packets;
                self.split_shares = shares;
                self.split_selected_share = 0;
                self.focus = Focus::SplitShares;
                self.set_ok("split ok");
            }
            Err(e) => self.set_err(format!("split error: {e}")),
        }

        Ok(())
    }

    fn do_combine(&mut self) -> Result<()> {
        self.invalidate_combine();
        let input = self.combine_shares_text.lines().join("\n");

        let passphrase = if self.combine_passphrase.is_empty() {
            None
        } else {
            Some(self.combine_passphrase.as_bytes())
        };

        match combine_shares(&input, self.combine_encoding, passphrase) {
            Ok((_packets, bytes, used_enc)) => {
                let recovered = Zeroizing::new(bytes);
                let recovered_text = String::from_utf8(recovered.as_slice().to_vec())
                    .ok()
                    .map(Zeroizing::new);

                self.combine_used_encoding = Some(used_enc);
                self.combine_recovered_text = recovered_text;
                self.combine_recovered = Some(recovered);
                self.set_ok(format!("combined ok ({})", used_enc.label()));
            }
            Err(e) => {
                self.set_err(format!("combine error: {e}"));
            }
        }

        Ok(())
    }

    fn next_tab(&mut self) {
        self.tab = match self.tab {
            TabId::Split => TabId::Combine,
            TabId::Combine => TabId::Split,
        };
        self.focus = match self.tab {
            TabId::Split => Focus::SplitSecret,
            TabId::Combine => Focus::CombineShares,
        };
    }

    fn prev_tab(&mut self) {
        self.next_tab();
    }

    fn next_focus(&mut self) {
        self.focus = match (self.tab, self.focus) {
            (TabId::Split, Focus::SplitSecret) => Focus::SplitK,
            (TabId::Split, Focus::SplitK) => Focus::SplitN,
            (TabId::Split, Focus::SplitN) => Focus::SplitEncoding,
            (TabId::Split, Focus::SplitEncoding) => Focus::SplitPassphrase,
            (TabId::Split, Focus::SplitPassphrase) => Focus::SplitShares,
            (TabId::Split, Focus::SplitShares) => Focus::SplitSecret,

            (TabId::Combine, Focus::CombineShares) => Focus::CombineEncoding,
            (TabId::Combine, Focus::CombineEncoding) => Focus::CombinePassphrase,
            (TabId::Combine, Focus::CombinePassphrase) => Focus::CombineShares,

            (TabId::Split, _) => Focus::SplitSecret,
            (TabId::Combine, _) => Focus::CombineShares,
        };
    }

    fn prev_focus(&mut self) {
        self.focus = match (self.tab, self.focus) {
            (TabId::Split, Focus::SplitSecret) => Focus::SplitShares,
            (TabId::Split, Focus::SplitK) => Focus::SplitSecret,
            (TabId::Split, Focus::SplitN) => Focus::SplitK,
            (TabId::Split, Focus::SplitEncoding) => Focus::SplitN,
            (TabId::Split, Focus::SplitPassphrase) => Focus::SplitEncoding,
            (TabId::Split, Focus::SplitShares) => Focus::SplitPassphrase,

            (TabId::Combine, Focus::CombineShares) => Focus::CombinePassphrase,
            (TabId::Combine, Focus::CombineEncoding) => Focus::CombineShares,
            (TabId::Combine, Focus::CombinePassphrase) => Focus::CombineEncoding,

            (TabId::Split, _) => Focus::SplitSecret,
            (TabId::Combine, _) => Focus::CombineShares,
        };
    }

    fn render(&mut self, f: &mut Frame) {
        let area = f.area();

        let outer = Layout::default()
            .direction(Direction::Vertical)
            .constraints([
                Constraint::Length(3),
                Constraint::Min(0),
                Constraint::Length(2),
            ])
            .split(area);

        self.render_header(f, outer[0]);

        match self.tab {
            TabId::Split => self.render_split(f, outer[1]),
            TabId::Combine => self.render_combine(f, outer[1]),
        }

        self.render_footer(f, outer[2]);

        if self.show_help {
            self.render_help(f, centered_rect(86, 86, area));
        }

        if let Some(modal) = self.modal.as_mut() {
            Self::render_modal(f, centered_rect(78, 45, area), modal, self.theme);
        }
    }

    fn render_header(&self, f: &mut Frame, area: Rect) {
        let titles = ["Split", "Combine"]
            .iter()
            .map(|t| Line::from(*t))
            .collect::<Vec<_>>();

        let idx = match self.tab {
            TabId::Split => 0,
            TabId::Combine => 1,
        };

        let tabs = Tabs::new(titles)
            .select(idx)
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .title(" safeparts ")
                    .border_style(Style::default().fg(self.theme.border)),
            )
            .highlight_style(
                Style::default()
                    .fg(self.theme.accent)
                    .add_modifier(Modifier::BOLD),
            );

        f.render_widget(tabs, area);
    }

    fn render_footer(&self, f: &mut Frame, area: Rect) {
        let shortcuts = match self.tab {
            TabId::Split => {
                "Enter split • Ctrl+L load • Ctrl+S export • Ctrl+C copy selected share • Tab focus • ? help • Ctrl+Q quit"
            }
            TabId::Combine => {
                "Enter combine • Ctrl+L load • Ctrl+S save • Ctrl+C copy • Tab focus • ? help • Ctrl+Q quit"
            }
        };

        let status_line = self.status.as_ref().map(|s| {
            let (label, color) = match s.kind {
                StatusKind::Info => ("info", self.theme.dim),
                StatusKind::Ok => ("ok", self.theme.ok),
                StatusKind::Error => ("error", self.theme.err),
            };

            Line::from(vec![
                Span::styled(format!("[{label}] "), Style::default().fg(color)),
                Span::raw(&s.msg),
            ])
        });

        let mut text = Text::default();
        if let Some(line) = status_line {
            text.lines.push(line);
        }
        text.lines.push(Line::from(Span::styled(
            shortcuts,
            Style::default().fg(self.theme.dim),
        )));

        let p = Paragraph::new(text)
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .border_style(Style::default().fg(self.theme.border)),
            )
            .wrap(Wrap { trim: true });
        f.render_widget(p, area);
    }

    fn render_split(&mut self, f: &mut Frame, area: Rect) {
        let layout = Layout::default()
            .direction(Direction::Horizontal)
            .constraints([Constraint::Percentage(52), Constraint::Percentage(48)])
            .split(area);

        let left = Layout::default()
            .direction(Direction::Vertical)
            .constraints([
                Constraint::Min(10),
                Constraint::Length(8),
                Constraint::Length(4),
            ])
            .split(layout[0]);

        let secret_title = if let Some(p) = self.split_secret_file.as_ref() {
            let suffix = self
                .split_secret_file_len
                .map(|len| format!(" ({len} bytes)"))
                .unwrap_or_default();
            format!("Secret (file: {}{suffix})", p.display())
        } else {
            "Secret (text)".to_string()
        };

        {
            let block = self.block_for_focus(self.focus == Focus::SplitSecret, secret_title);
            self.split_secret_text.set_block(block);
        }
        f.render_widget(&self.split_secret_text, left[0]);

        f.render_widget(self.split_settings_table(), left[1]);

        let tips = vec![
            Line::from(vec![
                Span::styled("Tip: ", Style::default().fg(self.theme.dim)),
                Span::raw("Tab to focus; ↑/↓ to change numeric/encoding."),
            ]),
            Line::from(vec![
                Span::styled("Copy: ", Style::default().fg(self.theme.dim)),
                Span::raw("Ctrl+C copies only the selected Recovery share."),
            ]),
        ];

        let tips = Paragraph::new(tips)
            .block(self.block("Tips"))
            .wrap(Wrap { trim: true });
        f.render_widget(tips, left[2]);

        let right = Layout::default()
            .direction(Direction::Vertical)
            .constraints([
                Constraint::Length(3),
                Constraint::Min(6),
                Constraint::Min(6),
            ])
            .split(layout[1]);

        let header = Paragraph::new(Line::from(vec![
            Span::styled(
                format!("{} shares", self.split_shares.len()),
                Style::default()
                    .fg(self.theme.accent)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(
                "  •  Ctrl+S exports one file/share",
                Style::default().fg(self.theme.dim),
            ),
        ]))
        .block(self.block("Output"));
        f.render_widget(header, right[0]);

        let items = self
            .split_shares
            .iter()
            .enumerate()
            .map(|(i, s)| ListItem::new(format!("#{:02}  {}", i + 1, preview(s))))
            .collect::<Vec<_>>();

        let list = List::new(items)
            .block(self.block_for_focus(self.focus == Focus::SplitShares, "Share list"))
            .highlight_style(
                Style::default()
                    .fg(self.theme.accent)
                    .add_modifier(Modifier::BOLD),
            )
            .highlight_symbol("▶ ");

        f.render_stateful_widget(list, right[1], &mut list_state(self.split_selected_share));

        let preview_text = self
            .split_shares
            .get(self.split_selected_share)
            .cloned()
            .unwrap_or_default();

        let preview = Paragraph::new(preview_text)
            .block(self.block("Selected share (copy-friendly)"))
            .wrap(Wrap { trim: false });
        f.render_widget(preview, right[2]);
    }

    fn split_settings_table(&self) -> Table<'_> {
        let rows = vec![
            settings_row(
                "k",
                format!("{}  (↑/↓)", self.split_k),
                self.focus == Focus::SplitK,
                self.theme,
            ),
            settings_row(
                "n",
                format!("{}  (↑/↓)", self.split_n),
                self.focus == Focus::SplitN,
                self.theme,
            ),
            settings_row(
                "encoding",
                format!("{}  (↑/↓)", self.split_encoding.label()),
                self.focus == Focus::SplitEncoding,
                self.theme,
            ),
            settings_row(
                "passphrase",
                format!(
                    "{}  (Ctrl+U clear)",
                    masked_passphrase(&self.split_passphrase)
                ),
                self.focus == Focus::SplitPassphrase,
                self.theme,
            ),
            Row::new(vec![
                Cell::from(Span::styled("actions", Style::default().fg(self.theme.dim))),
                Cell::from(Span::styled(
                    "Enter split • Ctrl+L load file",
                    Style::default().fg(self.theme.dim),
                )),
            ]),
        ];

        Table::new(rows, [Constraint::Length(12), Constraint::Min(0)])
            .block(self.block("Settings"))
            .column_spacing(1)
    }

    fn render_combine(&mut self, f: &mut Frame, area: Rect) {
        let layout = Layout::default()
            .direction(Direction::Horizontal)
            .constraints([Constraint::Percentage(52), Constraint::Percentage(48)])
            .split(area);

        let left = Layout::default()
            .direction(Direction::Vertical)
            .constraints([Constraint::Min(10), Constraint::Length(8)])
            .split(layout[0]);

        {
            let block = self.block_for_focus(self.focus == Focus::CombineShares, "Shares input");
            self.combine_shares_text.set_block(block);
        }
        f.render_widget(&self.combine_shares_text, left[0]);

        f.render_widget(self.combine_settings_table(), left[1]);

        let right = Layout::default()
            .direction(Direction::Vertical)
            .constraints([
                Constraint::Length(5),
                Constraint::Min(6),
                Constraint::Min(6),
            ])
            .split(layout[1]);

        let detected = self.combine_used_encoding.map(|e| e.label()).unwrap_or("-");

        let recovered_len = self
            .combine_recovered
            .as_ref()
            .map(|b| b.len())
            .unwrap_or(0);

        let meta = Paragraph::new(vec![
            Line::from(vec![
                Span::styled("Detected: ", Style::default().fg(self.theme.dim)),
                Span::styled(detected, Style::default().fg(self.theme.accent)),
            ]),
            Line::from(vec![
                Span::styled("Bytes: ", Style::default().fg(self.theme.dim)),
                Span::styled(
                    recovered_len.to_string(),
                    Style::default().fg(self.theme.accent),
                ),
            ]),
            Line::from(Span::styled(
                "Ctrl+C copies UTF-8 else base64",
                Style::default().fg(self.theme.dim),
            )),
        ])
        .block(self.block("Recovered"));
        f.render_widget(meta, right[0]);

        let utf8_text = self
            .combine_recovered_text
            .as_ref()
            .map(|s| s.as_str())
            .unwrap_or("");
        let base64_text = self
            .combine_recovered
            .as_ref()
            .map(|b| base64::engine::general_purpose::STANDARD.encode(b.as_slice()))
            .unwrap_or_default();

        let text_view = Paragraph::new(utf8_text)
            .block(self.block("Text view (UTF-8)"))
            .wrap(Wrap { trim: false });
        f.render_widget(text_view, right[1]);

        let base64_view = Paragraph::new(base64_text)
            .block(self.block("Bytes view (base64)"))
            .wrap(Wrap { trim: false });
        f.render_widget(base64_view, right[2]);
    }

    fn combine_settings_table(&self) -> Table<'_> {
        let rows = vec![
            settings_row(
                "encoding",
                format!("{}  (↑/↓)", self.combine_encoding.label()),
                self.focus == Focus::CombineEncoding,
                self.theme,
            ),
            settings_row(
                "passphrase",
                format!(
                    "{}  (Ctrl+U clear)",
                    masked_passphrase(&self.combine_passphrase)
                ),
                self.focus == Focus::CombinePassphrase,
                self.theme,
            ),
            Row::new(vec![
                Cell::from(Span::styled("actions", Style::default().fg(self.theme.dim))),
                Cell::from(Span::styled(
                    "Enter combine • Ctrl+L load files • Ctrl+S save",
                    Style::default().fg(self.theme.dim),
                )),
            ]),
        ];

        Table::new(rows, [Constraint::Length(12), Constraint::Min(0)])
            .block(self.block("Settings"))
            .column_spacing(1)
    }

    fn render_help(&self, f: &mut Frame, area: Rect) {
        f.render_widget(Clear, area);

        let block = Block::default()
            .borders(Borders::ALL)
            .title(" Help ")
            .border_style(Style::default().fg(self.theme.border));

        let content = vec![
            Line::from(Span::styled(
                "safeparts-tui",
                Style::default()
                    .fg(self.theme.accent)
                    .add_modifier(Modifier::BOLD),
            )),
            Line::from(""),
            Line::from(Span::styled(
                "Navigation",
                Style::default().add_modifier(Modifier::BOLD),
            )),
            Line::from("  Left/Right: switch Split/Combine"),
            Line::from("  Tab / Shift+Tab: change focus"),
            Line::from(""),
            Line::from(Span::styled(
                "Actions",
                Style::default().add_modifier(Modifier::BOLD),
            )),
            Line::from("  Enter: run split/combine"),
            Line::from("  Ctrl+L: load secret/share file(s)"),
            Line::from("  Ctrl+S: save/export"),
            Line::from("  Ctrl+C: copy selected Recovery share or recovered Secret"),
            Line::from("  Ctrl+V: paste into focused editor"),
            Line::from("  Ctrl+U: clear passphrase"),
            Line::from("  Ctrl+Q: quit"),
            Line::from(""),
            Line::from(Span::styled(
                "Input formats",
                Style::default().add_modifier(Modifier::BOLD),
            )),
            Line::from("  base64url/base58check: whitespace-separated shares"),
            Line::from("  mnemonics: one share per paragraph (blank-line separated)"),
        ];

        let p = Paragraph::new(content)
            .block(block)
            .alignment(Alignment::Left)
            .wrap(Wrap { trim: false });
        f.render_widget(p, area);
    }

    fn render_modal(f: &mut Frame, area: Rect, modal: &mut Modal, theme: Theme) {
        f.render_widget(Clear, area);

        let (title, helper) = match modal.kind {
            ModalKind::LoadSecretFile => (
                "Load secret file",
                "Paste a path and press Enter (Esc cancels)",
            ),
            ModalKind::LoadShareFiles => {
                ("Load share files", "One file path per line (Esc cancels)")
            }
            ModalKind::SaveSharesDir => (
                "Export shares",
                "Enter a directory; exports one file per share",
            ),
            ModalKind::SaveSecretFile => ("Save secret", "Enter file path for recovered bytes"),
        };

        let outer = Block::default()
            .borders(Borders::ALL)
            .title(format!(" {title} "))
            .border_style(Style::default().fg(theme.border));

        f.render_widget(outer, area);

        let inner = area.inner(Margin {
            vertical: 1,
            horizontal: 2,
        });

        let parts = Layout::default()
            .direction(Direction::Vertical)
            .constraints([
                Constraint::Length(2),
                Constraint::Min(1),
                Constraint::Length(1),
            ])
            .split(inner);

        let helper = Paragraph::new(Line::from(vec![
            Span::styled("Hint: ", Style::default().fg(theme.dim)),
            Span::raw(helper),
        ]));
        f.render_widget(helper, parts[0]);

        modal.input.set_block(
            Block::default()
                .borders(Borders::ALL)
                .title(" Path ")
                .border_style(Style::default().fg(theme.accent)),
        );
        f.render_widget(&modal.input, parts[1]);

        let footer = Paragraph::new(Line::from(Span::styled(
            "Enter confirm • Esc cancel",
            Style::default().fg(theme.dim),
        )))
        .alignment(Alignment::Center);
        f.render_widget(footer, parts[2]);
    }

    fn block(&self, title: impl Into<String>) -> Block<'static> {
        Block::default()
            .borders(Borders::ALL)
            .title(format!(" {} ", title.into()))
            .border_style(Style::default().fg(self.theme.border))
    }

    fn block_for_focus(&self, active: bool, title: impl Into<String>) -> Block<'static> {
        let mut block = self.block(title);
        if active {
            block = block.border_style(Style::default().fg(self.theme.accent));
        }
        block
    }
}

fn is_control_key(key: KeyEvent, character: char) -> bool {
    key.modifiers.contains(KeyModifiers::CONTROL) && key.code == KeyCode::Char(character)
}

fn settings_row(label: &'static str, value: String, active: bool, theme: Theme) -> Row<'static> {
    let value_style = if active {
        Style::default()
            .fg(theme.accent)
            .add_modifier(Modifier::BOLD)
    } else {
        Style::default().fg(theme.accent)
    };

    Row::new(vec![
        Cell::from(Span::styled(
            label,
            Style::default().add_modifier(Modifier::BOLD),
        )),
        Cell::from(Span::styled(value, value_style)),
    ])
}

fn masked_passphrase(p: &str) -> String {
    if p.is_empty() {
        "(none)".to_string()
    } else {
        let len = p.chars().count();
        let shown = len.min(12);
        format!("{} (len={len})", "•".repeat(shown))
    }
}

fn preview(s: &str) -> String {
    let max = 36;
    if s.len() <= max {
        s.to_string()
    } else {
        format!("{}…", &s[..max])
    }
}

fn cycle_encoding(current: Encoding, delta: i32, allowed: &[Encoding]) -> Encoding {
    let idx = allowed.iter().position(|e| *e == current).unwrap_or(0) as i32;
    let len = allowed.len() as i32;
    let next = (idx + delta).rem_euclid(len) as usize;
    allowed[next]
}

fn centered_rect(percent_x: u16, percent_y: u16, r: Rect) -> Rect {
    let popup_layout = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Percentage((100 - percent_y) / 2),
            Constraint::Percentage(percent_y),
            Constraint::Percentage((100 - percent_y) / 2),
        ])
        .split(r);

    Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Percentage((100 - percent_x) / 2),
            Constraint::Percentage(percent_x),
            Constraint::Percentage((100 - percent_x) / 2),
        ])
        .split(popup_layout[1])[1]
}

fn list_state(selected: usize) -> ratatui::widgets::ListState {
    let mut state = ratatui::widgets::ListState::default();
    state.select(Some(selected));
    state
}

#[cfg(test)]
mod tests {
    use ratatui::backend::TestBackend;

    use super::*;

    fn status_message(app: &App) -> &str {
        app.status
            .as_ref()
            .map(|status| status.msg.as_str())
            .unwrap_or("")
    }

    fn combine_input(shares: &[String]) -> String {
        shares.join("\n\n")
    }

    fn key(app: &mut App, code: KeyCode) {
        app.on_key(KeyEvent::new(code, KeyModifiers::NONE)).unwrap();
    }

    fn ctrl(app: &mut App, character: char) {
        app.on_key(KeyEvent::new(
            KeyCode::Char(character),
            KeyModifiers::CONTROL,
        ))
        .unwrap();
    }

    fn focus(app: &mut App, target: Focus) {
        for _ in 0..6 {
            if app.focus == target {
                return;
            }
            key(app, KeyCode::Tab);
        }
        panic!("focus target not in active tab");
    }

    fn split_app() -> App {
        let mut app = App::new();
        app.clipboard = Clipboard::recording();
        for c in "synthetic lifetime Secret".chars() {
            key(&mut app, KeyCode::Char(c));
        }
        key(&mut app, KeyCode::Enter);
        assert_eq!(status_message(&app), "split ok");
        app
    }

    fn assert_split_unavailable(app: &mut App) {
        focus(app, Focus::SplitShares);
        let writes = app.clipboard.writes().len();
        ctrl(app, 'c');
        assert_eq!(status_message(app), "no shares to copy");
        assert_eq!(app.clipboard.writes().len(), writes);
        ctrl(app, 's');
        assert!(app.modal.is_none());
        assert_eq!(status_message(app), "no shares to save");
        assert!(app.split_shares.is_empty());
        assert!(app.split_packets.is_empty());
        assert_eq!(app.split_selected_share, 0);
    }

    #[test]
    fn split_keyboard_edits_remove_copy_and_export_results() {
        for (target, edit) in [
            (Focus::SplitSecret, KeyCode::Char('x')),
            (Focus::SplitSecret, KeyCode::Backspace),
            (Focus::SplitK, KeyCode::Up),
            (Focus::SplitK, KeyCode::Down),
            (Focus::SplitN, KeyCode::Up),
            (Focus::SplitN, KeyCode::Down),
            (Focus::SplitEncoding, KeyCode::Up),
            (Focus::SplitEncoding, KeyCode::Down),
            (Focus::SplitPassphrase, KeyCode::Char('p')),
        ] {
            let mut app = split_app();
            focus(&mut app, target);
            key(&mut app, edit);
            assert_split_unavailable(&mut app);
            key(&mut app, KeyCode::Enter);
            ctrl(&mut app, 'c');
            assert_eq!(app.clipboard.writes().len(), 1);
            ctrl(&mut app, 's');
            assert_eq!(
                app.modal.as_ref().map(|m| m.kind),
                Some(ModalKind::SaveSharesDir)
            );
        }
    }

    fn recover_app() -> App {
        let mut app = split_app();
        let input = combine_input(&app.split_shares[..2]);
        key(&mut app, KeyCode::Right);
        for c in input.chars() {
            key(&mut app, KeyCode::Char(c));
        }
        key(&mut app, KeyCode::Enter);
        assert!(status_message(&app).starts_with("combined ok"));
        app
    }

    fn assert_recovery_unavailable(app: &mut App) {
        let writes = app.clipboard.writes().len();
        ctrl(app, 'c');
        assert_eq!(status_message(app), "nothing to copy");
        assert_eq!(app.clipboard.writes().len(), writes);
        ctrl(app, 's');
        assert!(app.modal.is_none());
        assert_eq!(status_message(app), "nothing to save");
        assert!(app.combine_recovered.is_none());
        assert!(app.combine_recovered_text.is_none());
        assert!(app.combine_used_encoding.is_none());
    }

    #[test]
    fn recover_keyboard_edits_remove_copy_and_save_results() {
        for (target, edit) in [
            (Focus::CombineShares, KeyCode::Char('x')),
            (Focus::CombineShares, KeyCode::Backspace),
            (Focus::CombineEncoding, KeyCode::Up),
            (Focus::CombineEncoding, KeyCode::Down),
            (Focus::CombinePassphrase, KeyCode::Char('p')),
        ] {
            let mut app = recover_app();
            focus(&mut app, target);
            key(&mut app, edit);
            assert_recovery_unavailable(&mut app);
        }
    }

    #[test]
    fn clipboard_paste_invalidates_only_the_edited_operation() {
        for target in [Focus::SplitSecret, Focus::SplitPassphrase] {
            let mut app = split_app();
            app.clipboard.set_text("synthetic paste").unwrap();
            focus(&mut app, target);
            ctrl(&mut app, 'v');
            assert_split_unavailable(&mut app);
        }
        for target in [Focus::CombineShares, Focus::CombinePassphrase] {
            let mut app = recover_app();
            app.clipboard.set_text("synthetic paste").unwrap();
            focus(&mut app, target);
            ctrl(&mut app, 'v');
            assert_recovery_unavailable(&mut app);
            // Editing Recover must not discard the Split result.
            key(&mut app, KeyCode::Left);
            focus(&mut app, Focus::SplitShares);
            ctrl(&mut app, 'c');
            assert_eq!(status_message(&app), "copied to clipboard");
        }
    }

    fn submit_path(app: &mut App, path: &std::path::Path) {
        for c in path.to_str().unwrap().chars() {
            key(app, KeyCode::Char(c));
        }
        key(app, KeyCode::Enter);
    }

    #[test]
    fn successful_file_loading_invalidates_results_until_fresh_success() {
        let dir = tempfile::tempdir().unwrap();
        let secret_path = dir.path().join("secret.bin");
        let export_path = dir.path().join("export");
        let recovered_path = dir.path().join("recovered.bin");
        let secret = b"synthetic replacement Secret\0\xff";
        fs::write(&secret_path, secret).unwrap();
        let mut app = split_app();
        ctrl(&mut app, 'l');
        submit_path(&mut app, &secret_path);
        assert_split_unavailable(&mut app);
        assert!(!export_path.exists());
        key(&mut app, KeyCode::Enter);
        ctrl(&mut app, 's');
        submit_path(&mut app, &export_path);
        let share_path = fs::read_dir(&export_path)
            .unwrap()
            .next()
            .unwrap()
            .unwrap()
            .path();
        assert_eq!(fs::read_dir(&export_path).unwrap().count(), 3);

        let mut app = recover_app();
        let shares_path = dir.path().join("shares.txt");
        let mut shares = fs::read_dir(&export_path)
            .unwrap()
            .map(|entry| fs::read_to_string(entry.unwrap().path()).unwrap())
            .collect::<Vec<_>>();
        shares.sort();
        fs::write(&shares_path, shares.join("\n")).unwrap();
        ctrl(&mut app, 'l');
        submit_path(&mut app, &shares_path);
        assert_recovery_unavailable(&mut app);
        assert!(!recovered_path.exists());
        key(&mut app, KeyCode::Enter);
        ctrl(&mut app, 'c');
        assert_eq!(
            app.clipboard.writes().last().unwrap(),
            &base64::engine::general_purpose::STANDARD.encode(secret)
        );
        ctrl(&mut app, 's');
        submit_path(&mut app, &recovered_path);
        assert_eq!(fs::read(&recovered_path).unwrap(), secret);

        // Loading fewer shares clears the successful binary recovery too.
        ctrl(&mut app, 'l');
        submit_path(&mut app, &share_path);
        assert_recovery_unavailable(&mut app);
    }

    #[test]
    fn failed_or_rejected_split_attempts_cannot_export_an_earlier_result() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("mutable-secret.bin");
        for replacement in [Some(b"".as_slice()), None] {
            fs::write(&path, b"synthetic file Secret").unwrap();
            let mut app = App::new();
            app.clipboard = Clipboard::recording();
            ctrl(&mut app, 'l');
            submit_path(&mut app, &path);
            key(&mut app, KeyCode::Enter);
            assert_eq!(status_message(&app), "split ok");
            if let Some(bytes) = replacement {
                fs::write(&path, bytes).unwrap();
                key(&mut app, KeyCode::Enter);
                assert_eq!(status_message(&app), "secret is empty");
            } else {
                fs::remove_file(&path).unwrap();
                assert!(
                    app.on_key(KeyEvent::new(KeyCode::Enter, KeyModifiers::NONE))
                        .is_err()
                );
            }
            assert_split_unavailable(&mut app);
        }
    }

    #[test]
    fn adding_passphrase_protection_requires_fresh_export_and_recovery_passphrase() {
        let dir = tempfile::tempdir().unwrap();
        let export = dir.path().join("protected");
        let recovered = dir.path().join("recovered.txt");
        let mut app = split_app();
        focus(&mut app, Focus::SplitPassphrase);
        for c in "synthetic protection".chars() {
            key(&mut app, KeyCode::Char(c));
        }
        assert_split_unavailable(&mut app);
        key(&mut app, KeyCode::Enter);
        ctrl(&mut app, 'c');
        assert_eq!(app.clipboard.writes().len(), 1);
        ctrl(&mut app, 's');
        submit_path(&mut app, &export);
        let shares = fs::read_dir(&export)
            .unwrap()
            .map(|entry| fs::read_to_string(entry.unwrap().path()).unwrap())
            .collect::<Vec<_>>();
        assert_eq!(shares.len(), 3);
        app.clipboard.set_text(&shares.join("\n")).unwrap();
        key(&mut app, KeyCode::Right);
        ctrl(&mut app, 'v');
        key(&mut app, KeyCode::Enter);
        assert!(status_message(&app).starts_with("combine error:"));
        assert_recovery_unavailable(&mut app);
        focus(&mut app, Focus::CombinePassphrase);
        for c in "synthetic protection".chars() {
            key(&mut app, KeyCode::Char(c));
        }
        key(&mut app, KeyCode::Enter);
        ctrl(&mut app, 'c');
        assert_eq!(
            app.clipboard.writes().last().unwrap(),
            "synthetic lifetime Secret"
        );
        ctrl(&mut app, 's');
        submit_path(&mut app, &recovered);
        assert_eq!(fs::read(&recovered).unwrap(), b"synthetic lifetime Secret");

        // Deleting and clearing an existing passphrase invalidate both operations.
        key(&mut app, KeyCode::Backspace);
        assert_recovery_unavailable(&mut app);
        key(&mut app, KeyCode::Char('n'));
        key(&mut app, KeyCode::Enter);
        ctrl(&mut app, 'u');
        assert_recovery_unavailable(&mut app);
        key(&mut app, KeyCode::Enter);
        assert!(status_message(&app).starts_with("combine error:"));
        assert_recovery_unavailable(&mut app);

        key(&mut app, KeyCode::Left);
        focus(&mut app, Focus::SplitPassphrase);
        key(&mut app, KeyCode::Backspace);
        assert_split_unavailable(&mut app);
        key(&mut app, KeyCode::Enter);
        focus(&mut app, Focus::SplitPassphrase);
        ctrl(&mut app, 'u');
        assert_split_unavailable(&mut app);
    }

    #[test]
    fn navigation_and_noop_actions_preserve_valid_results() {
        let mut app = split_app();
        let shares = app.split_shares.clone();
        key(&mut app, KeyCode::Down);
        ctrl(&mut app, 'c');
        assert_eq!(app.clipboard.writes().last(), Some(&shares[1]));
        key(&mut app, KeyCode::Up);
        for target in [Focus::SplitSecret, Focus::SplitPassphrase, Focus::SplitK] {
            focus(&mut app, target);
            key(&mut app, KeyCode::Home);
            key(&mut app, KeyCode::End);
            app.clipboard.set_text("").unwrap();
            ctrl(&mut app, 'v');
        }
        focus(&mut app, Focus::SplitPassphrase);
        key(&mut app, KeyCode::Backspace);
        ctrl(&mut app, 'u');
        ctrl(&mut app, 'l');
        key(&mut app, KeyCode::Esc);
        key(&mut app, KeyCode::F(1));
        key(&mut app, KeyCode::Esc);
        focus(&mut app, Focus::SplitShares);
        ctrl(&mut app, 'c');
        assert_eq!(app.clipboard.writes().last(), Some(&shares[0]));
        ctrl(&mut app, 's');
        assert!(app.modal.is_some());
        key(&mut app, KeyCode::Esc);

        let mut app = recover_app();
        for target in [
            Focus::CombineShares,
            Focus::CombinePassphrase,
            Focus::CombineEncoding,
        ] {
            focus(&mut app, target);
            key(&mut app, KeyCode::Home);
            key(&mut app, KeyCode::End);
            app.clipboard.set_text("").unwrap();
            ctrl(&mut app, 'v');
        }
        focus(&mut app, Focus::CombinePassphrase);
        key(&mut app, KeyCode::Backspace);
        ctrl(&mut app, 'u');
        ctrl(&mut app, 'c');
        assert_eq!(
            app.clipboard.writes().last().unwrap(),
            "synthetic lifetime Secret"
        );
        ctrl(&mut app, 's');
        assert!(app.modal.is_some());
    }

    #[test]
    fn pasting_identical_selected_text_preserves_the_result() {
        let mut app = split_app();
        focus(&mut app, Focus::SplitSecret);
        app.on_key(KeyEvent::new(KeyCode::Home, KeyModifiers::SHIFT))
            .unwrap();
        app.clipboard.set_text("synthetic lifetime Secret").unwrap();
        ctrl(&mut app, 'v');
        focus(&mut app, Focus::SplitShares);
        ctrl(&mut app, 'c');
        assert_eq!(status_message(&app), "copied to clipboard");
        ctrl(&mut app, 's');
        assert!(app.modal.is_some());
    }

    #[test]
    fn text_clear_delete_and_undo_invalidate_results() {
        for recover in [false, true] {
            for clear in [false, true] {
                let mut app = if recover { recover_app() } else { split_app() };
                let target = if recover {
                    Focus::CombineShares
                } else {
                    Focus::SplitSecret
                };
                focus(&mut app, target);
                key(&mut app, KeyCode::Home);
                if clear {
                    ctrl(&mut app, 'k'); // Delete from cursor to end of line.
                } else {
                    key(&mut app, KeyCode::Delete);
                }
                if recover {
                    assert_recovery_unavailable(&mut app);
                } else {
                    assert_split_unavailable(&mut app);
                    focus(&mut app, target);
                }
                ctrl(&mut app, 'u'); // TextArea undo restores input, not its old result.
                if recover {
                    assert_recovery_unavailable(&mut app);
                } else {
                    assert_split_unavailable(&mut app);
                }
            }
        }
    }

    #[test]
    fn bounded_settings_and_file_cursor_movement_preserve_results() {
        let mut app = split_app();
        focus(&mut app, Focus::SplitK);
        key(&mut app, KeyCode::Down);
        key(&mut app, KeyCode::Enter);
        focus(&mut app, Focus::SplitK);
        key(&mut app, KeyCode::Down); // Threshold is already one.
        focus(&mut app, Focus::SplitShares);
        ctrl(&mut app, 'c');
        assert_eq!(status_message(&app), "copied to clipboard");
        focus(&mut app, Focus::SplitN);
        key(&mut app, KeyCode::Down);
        key(&mut app, KeyCode::Down);
        key(&mut app, KeyCode::Enter);
        focus(&mut app, Focus::SplitN);
        key(&mut app, KeyCode::Down); // Share count is already one.
        focus(&mut app, Focus::SplitK);
        key(&mut app, KeyCode::Up); // Threshold cannot exceed Share count.
        focus(&mut app, Focus::SplitShares);
        ctrl(&mut app, 'c');
        assert_eq!(status_message(&app), "copied to clipboard");

        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("secret.bin");
        fs::write(&path, b"synthetic file Secret").unwrap();
        ctrl(&mut app, 'l');
        submit_path(&mut app, &path);
        key(&mut app, KeyCode::Enter);
        focus(&mut app, Focus::SplitSecret);
        key(&mut app, KeyCode::Home);
        key(&mut app, KeyCode::Backspace); // Empty editor: keep file input.
        app.clipboard.set_text("").unwrap();
        ctrl(&mut app, 'v');
        assert_eq!(app.split_secret_file.as_ref(), Some(&path));
        key(&mut app, KeyCode::Enter);
        assert_eq!(status_message(&app), "split ok");
        ctrl(&mut app, 'c');
        assert_eq!(status_message(&app), "copied to clipboard");
        focus(&mut app, Focus::SplitSecret);
        key(&mut app, KeyCode::Char('x'));
        assert!(app.split_secret_file.is_none());
        assert_split_unavailable(&mut app);
    }

    #[test]
    fn focus_navigation_wraps_within_each_tab() {
        let mut app = App::new();

        for expected in [
            Focus::SplitK,
            Focus::SplitN,
            Focus::SplitEncoding,
            Focus::SplitPassphrase,
            Focus::SplitShares,
            Focus::SplitSecret,
        ] {
            app.next_focus();
            assert_eq!(app.focus, expected);
        }

        app.next_tab();
        assert_eq!(app.focus, Focus::CombineShares);
        app.prev_focus();
        assert_eq!(app.focus, Focus::CombinePassphrase);
        app.next_focus();
        assert_eq!(app.focus, Focus::CombineShares);
    }

    #[test]
    fn load_and_save_shortcuts_preserve_modal_and_status_behavior() {
        let mut app = App::new();

        app.on_key(KeyEvent::new(KeyCode::Char('l'), KeyModifiers::CONTROL))
            .unwrap();
        assert_eq!(
            app.modal.as_ref().map(|modal| modal.kind),
            Some(ModalKind::LoadSecretFile)
        );

        app.on_key(KeyEvent::new(KeyCode::Esc, KeyModifiers::NONE))
            .unwrap();
        assert!(app.modal.is_none());

        app.on_key(KeyEvent::new(KeyCode::Char('s'), KeyModifiers::CONTROL))
            .unwrap();
        assert_eq!(
            app.status.as_ref().map(|status| status.msg.as_str()),
            Some("no shares to save")
        );
        assert!(app.modal.is_none());
    }

    #[test]
    fn copy_shortcut_keeps_split_shares_individual_and_copies_recovered_secret() {
        let mut app = App::new();
        app.clipboard = Clipboard::recording();
        app.split_shares = vec![
            "synthetic Recovery share one".to_string(),
            "synthetic Recovery share two".to_string(),
            "synthetic Recovery share three".to_string(),
        ];

        app.focus = Focus::SplitSecret;
        app.on_key(KeyEvent::new(KeyCode::Char('c'), KeyModifiers::CONTROL))
            .unwrap();
        assert!(app.clipboard.writes().is_empty());
        assert_eq!(status_message(&app), "focus Recovery shares to copy one");

        app.focus = Focus::SplitShares;
        app.split_selected_share = 1;
        app.on_key(KeyEvent::new(KeyCode::Char('c'), KeyModifiers::CONTROL))
            .unwrap();
        assert_eq!(
            app.clipboard.writes(),
            &["synthetic Recovery share two".to_string()]
        );

        app.next_tab();
        app.combine_recovered_text = Some(Zeroizing::new("synthetic recovered Secret".to_string()));
        app.on_key(KeyEvent::new(KeyCode::Char('c'), KeyModifiers::CONTROL))
            .unwrap();
        assert_eq!(
            app.clipboard.writes(),
            &[
                "synthetic Recovery share two".to_string(),
                "synthetic recovered Secret".to_string(),
            ]
        );
    }

    #[test]
    fn encoding_cycles_wrap_in_both_directions() {
        assert_eq!(
            cycle_encoding(Encoding::Base64url, -1, Encoding::SPLIT),
            Encoding::MnemoBip39
        );
        assert_eq!(
            cycle_encoding(Encoding::MnemoBip39, 1, Encoding::SPLIT),
            Encoding::Base64url
        );
    }

    #[test]
    fn split_and_recover_workflows_cover_encodings_and_passphrases() {
        let cases = [
            (Encoding::Base64url, None),
            (Encoding::MnemoWords, Some("synthetic passphrase")),
            (Encoding::MnemoBip39, None),
        ];

        for (encoding, passphrase) in cases {
            let secret = format!("synthetic TUI workflow for {}", encoding.label());
            let mut app = App::new();
            app.split_secret_text.insert_str(&secret);
            app.split_k = 2;
            app.split_n = 3;
            app.split_encoding = encoding;
            if let Some(passphrase) = passphrase {
                app.split_passphrase.push_str(passphrase);
            }

            app.do_split().unwrap();
            assert_eq!(app.split_shares.len(), 3);
            assert_eq!(app.focus, Focus::SplitShares);
            assert_eq!(status_message(&app), "split ok");

            let recovery_input = combine_input(&app.split_shares[..2]);
            app.next_tab();
            app.combine_shares_text.insert_str(recovery_input);
            if let Some(passphrase) = passphrase {
                app.combine_passphrase.push_str(passphrase);
            }

            app.do_combine().unwrap();
            assert_eq!(
                app.combine_recovered.as_deref().map(Vec::as_slice),
                Some(secret.as_bytes())
            );
            assert_eq!(app.combine_used_encoding, Some(encoding));
            assert!(status_message(&app).starts_with("combined ok"));
        }
    }

    #[test]
    fn recovery_failures_are_sanitized_and_clear_previous_output() {
        let (_, shares) = crate::domain::split_secret(
            b"synthetic error-path secret",
            2,
            3,
            Encoding::Base64url,
            Some(b"correct passphrase"),
        )
        .unwrap();
        let (_, other_set) = crate::domain::split_secret(
            b"different synthetic secret",
            2,
            3,
            Encoding::Base64url,
            Some(b"correct passphrase"),
        )
        .unwrap();

        let cases = [
            (
                combine_input(&shares[..1]),
                "correct passphrase",
                "combine failed",
            ),
            (
                combine_input(&[shares[0].clone(), shares[0].clone()]),
                "correct passphrase",
                "combine failed",
            ),
            (
                combine_input(&[shares[0].clone(), other_set[1].clone()]),
                "correct passphrase",
                "combine failed",
            ),
            (
                combine_input(&shares[..2]),
                "wrong passphrase",
                "combine failed",
            ),
            (
                "SECRET-SHARE-TEXT".to_string(),
                "correct passphrase",
                "could not detect share encoding",
            ),
        ];

        for (input, passphrase, expected) in cases {
            let mut app = App::new();
            app.next_tab();
            app.combine_recovered = Some(Zeroizing::new(b"stale output".to_vec()));
            app.combine_recovered_text = Some(Zeroizing::new("stale output".to_string()));
            app.combine_used_encoding = Some(Encoding::Base64url);
            app.combine_shares_text.insert_str(&input);
            app.combine_passphrase.push_str(passphrase);

            app.do_combine().unwrap();

            let status = status_message(&app);
            assert!(
                status.contains(expected),
                "unexpected failure class: {status}"
            );
            assert_eq!(
                app.status.as_ref().map(|status| status.kind),
                Some(StatusKind::Error)
            );
            for sensitive in shares.iter().chain(other_set.iter()) {
                assert!(!status.contains(sensitive));
            }
            for sensitive in [
                "SECRET-SHARE-TEXT",
                "synthetic error-path secret",
                "different synthetic secret",
                "correct passphrase",
                "wrong passphrase",
            ] {
                assert!(!status.contains(sensitive));
            }
            assert!(app.combine_recovered.is_none());
            assert!(app.combine_recovered_text.is_none());
            assert!(app.combine_used_encoding.is_none());
        }
    }

    #[test]
    fn file_loading_and_saving_supports_a_headless_round_trip() {
        let directory = tempfile::tempdir().unwrap();
        let secret_path = directory.path().join("input.bin");
        let recovered_path = directory.path().join("recovered.bin");
        let secret = b"synthetic file-backed TUI secret\0\xff";
        fs::write(&secret_path, secret).unwrap();

        let mut app = App::new();
        app.apply_modal(ModalKind::LoadSecretFile, secret_path.display().to_string())
            .unwrap();
        app.split_encoding = Encoding::MnemoWords;
        app.do_split().unwrap();

        let share_paths = app
            .split_shares
            .iter()
            .take(2)
            .enumerate()
            .map(|(index, share)| {
                let path = directory.path().join(format!("share-{index}.txt"));
                fs::write(&path, format!("{share}\n")).unwrap();
                path
            })
            .collect::<Vec<_>>();

        app.next_tab();
        app.apply_modal(
            ModalKind::LoadShareFiles,
            share_paths
                .iter()
                .map(|path| path.display().to_string())
                .collect::<Vec<_>>()
                .join("\n"),
        )
        .unwrap();
        app.do_combine().unwrap();
        app.apply_modal(
            ModalKind::SaveSecretFile,
            recovered_path.display().to_string(),
        )
        .unwrap();

        assert_eq!(fs::read(recovered_path).unwrap(), secret);
        assert_eq!(status_message(&app), "saved recovered secret");
    }

    #[test]
    fn keyboard_modal_status_and_render_paths_are_headless() {
        let mut app = App::new();
        assert!(
            !app.on_key(KeyEvent::new(KeyCode::Char('x'), KeyModifiers::NONE))
                .unwrap()
        );
        assert_eq!(app.split_secret_text.lines(), ["x"]);

        app.focus = Focus::SplitPassphrase;
        app.on_key(KeyEvent::new(KeyCode::Char('a'), KeyModifiers::NONE))
            .unwrap();
        app.on_key(KeyEvent::new(KeyCode::Backspace, KeyModifiers::NONE))
            .unwrap();
        app.split_passphrase.push_str("clear me");
        app.on_key(KeyEvent::new(KeyCode::Char('u'), KeyModifiers::CONTROL))
            .unwrap();
        assert!(app.split_passphrase.is_empty());

        app.on_key(KeyEvent::new(KeyCode::Char('?'), KeyModifiers::NONE))
            .unwrap();
        assert!(app.show_help);
        app.on_key(KeyEvent::new(KeyCode::Esc, KeyModifiers::NONE))
            .unwrap();
        assert!(!app.show_help);

        app.on_load();
        app.on_modal_key(KeyEvent::new(KeyCode::Esc, KeyModifiers::NONE))
            .unwrap();
        assert!(app.modal.is_none());

        app.set_info("expiring status");
        app.status.as_mut().unwrap().at = Instant::now() - Duration::from_secs(4);
        app.expire_status();
        assert!(app.status.is_none());

        assert!(
            app.on_key(KeyEvent::new(KeyCode::Char('q'), KeyModifiers::CONTROL))
                .unwrap()
        );

        let backend = TestBackend::new(120, 40);
        let mut terminal = Terminal::new(backend).unwrap();
        terminal.draw(|frame| app.render(frame)).unwrap();

        app.split_secret_text.insert_str("rendered secret");
        app.do_split().unwrap();
        app.show_help = true;
        terminal.draw(|frame| app.render(frame)).unwrap();
        app.show_help = false;

        for kind in [
            ModalKind::LoadSecretFile,
            ModalKind::LoadShareFiles,
            ModalKind::SaveSharesDir,
            ModalKind::SaveSecretFile,
        ] {
            app.modal = Some(Modal::new(kind, "synthetic path"));
            terminal.draw(|frame| app.render(frame)).unwrap();
        }
        app.modal = None;

        let input = combine_input(&app.split_shares[..2]);
        app.next_tab();
        app.combine_shares_text.insert_str(input);
        app.do_combine().unwrap();
        terminal.draw(|frame| app.render(frame)).unwrap();
        app.combine_recovered = Some(Zeroizing::new(vec![0xff, 0x00]));
        app.combine_recovered_text = None;
        terminal.draw(|frame| app.render(frame)).unwrap();
    }

    #[cfg(unix)]
    #[test]
    fn saved_recovery_shares_and_secret_are_owner_only() {
        use std::os::unix::fs::PermissionsExt;

        let directory = tempfile::tempdir().unwrap();
        let mut app = App::new();
        let (packets, shares) = crate::domain::split_secret(
            b"synthetic private TUI output",
            1,
            1,
            Encoding::Base64url,
            None,
        )
        .unwrap();
        app.split_packets = packets;
        app.split_shares = shares;
        app.apply_modal(
            ModalKind::SaveSharesDir,
            directory.path().display().to_string(),
        )
        .unwrap();

        let share_path = fs::read_dir(directory.path())
            .unwrap()
            .next()
            .unwrap()
            .unwrap()
            .path();
        assert_eq!(
            fs::metadata(share_path).unwrap().permissions().mode() & 0o077,
            0
        );

        let secret_path = directory.path().join("recovered.bin");
        fs::write(&secret_path, b"old").unwrap();
        fs::set_permissions(&secret_path, fs::Permissions::from_mode(0o644)).unwrap();
        app.combine_recovered = Some(Zeroizing::new(b"synthetic private TUI output".to_vec()));
        app.apply_modal(ModalKind::SaveSecretFile, secret_path.display().to_string())
            .unwrap();

        assert_eq!(
            fs::metadata(&secret_path).unwrap().permissions().mode() & 0o077,
            0
        );
        assert_eq!(
            fs::read(secret_path).unwrap(),
            b"synthetic private TUI output"
        );
    }
}
