//! CSSCMP1 tree benchmark. Parsing, creation and destruction are outside timers.
use std::{env, fs, str::SplitWhitespace, time::Instant};
use taffy::prelude::*;

type Result<T> = std::result::Result<T, String>;
struct Reader<'a>(SplitWhitespace<'a>);
impl Reader<'_> {
    fn token(&mut self) -> Result<&str> {
        self.0.next().ok_or_else(|| "Truncated fixture".into())
    }
    fn number<T: std::str::FromStr>(&mut self) -> Result<T> {
        self.token()?.parse().map_err(|_| "Invalid number".into())
    }
    fn tracks(&mut self, count: usize) -> Result<Vec<GridTemplateComponent<String>>> {
        if count > 1000 {
            return Err("Too many tracks".into());
        }
        (0..count)
            .map(|_| {
                let kind = self.token()?.to_owned();
                let value: f32 = self.number()?;
                if !value.is_finite() || value < 0.0 {
                    return Err("Invalid track value".into());
                }
                match kind.as_str() {
                    "F" => Ok(length(value)),
                    "W" => Ok(minmax(length(0.0), fr(value))),
                    "A" => Ok(auto()),
                    _ => Err("Unknown track".into()),
                }
            })
            .collect()
    }
}
struct Node {
    parent: i32,
    style: Style,
    measure: u8,
    preferred: Size<f32>,
}
struct Fixture {
    nodes: Vec<Node>,
    sizes: Vec<Size<f32>>,
    measured: Vec<usize>,
}
fn dimension(v: f32) -> Result<Dimension> {
    match v {
        -1.0 => Ok(auto()),
        -2.0 => Ok(percent(1.0)),
        v if v.is_finite() && v >= 0.0 => Ok(length(v)),
        _ => Err("Invalid dimension".into()),
    }
}
fn read(path: &str) -> Result<Fixture> {
    let text = fs::read_to_string(path).map_err(|e| e.to_string())?;
    let mut r = Reader(text.split_whitespace());
    if r.token()? != "CSSCMP1" {
        return Err("Invalid fixture magic".into());
    }
    let _name = r.token()?;
    let count: usize = r.number()?;
    let frames: usize = r.number()?;
    if !(2..=100001).contains(&count) || !(2..=100).contains(&frames) {
        return Err("Invalid fixture shape".into());
    }
    let mut nodes = Vec::with_capacity(count);
    let mut measured = Vec::new();
    let mut ancestors = Vec::new();
    let mut has_children = vec![false; count];
    for i in 0..count {
        let parent: i32 = r.number()?;
        if (i == 0 && parent != -1) || (i > 0 && (parent < 0 || parent as usize >= i)) {
            return Err("Invalid parent".into());
        }
        if i > 0 {
            while ancestors.last().is_some_and(|p| *p != parent) {
                ancestors.pop();
            }
            if ancestors.is_empty() {
                return Err("Fixture must use depth-first node order".into());
            }
            has_children[parent as usize] = true;
        }
        ancestors.push(i as i32);
        let display = match r.token()? {
            "F" => Display::Flex,
            "G" => Display::Grid,
            "B" => Display::Block,
            _ => return Err("Unknown display".into()),
        };
        let direction = match r.token()? {
            "R" => FlexDirection::Row,
            "C" => FlexDirection::Column,
            _ => return Err("Unknown flex direction".into()),
        };
        let wrap = match r.number::<u8>()? {
            0 => FlexWrap::NoWrap,
            1 => FlexWrap::Wrap,
            _ => return Err("Unknown wrap".into()),
        };
        let width = dimension(r.number()?)?;
        let height = dimension(r.number()?)?;
        let basis = dimension(r.number()?)?;
        let grow: f32 = r.number()?;
        let shrink: f32 = r.number()?;
        let padding: f32 = r.number()?;
        let margin: f32 = r.number()?;
        let gap: f32 = r.number()?;
        let column: i16 = r.number()?;
        let column_span: u16 = r.number()?;
        let row: i16 = r.number()?;
        let row_span: u16 = r.number()?;
        let measure: u8 = r.number()?;
        let preferred = Size {
            width: r.number()?,
            height: r.number()?,
        };
        let cols: usize = r.number()?;
        let rows: usize = r.number()?;
        if column < 0
            || column > 1000
            || row < 0
            || row > 1000
            || column_span == 0
            || column_span > 1000
            || row_span == 0
            || row_span > 1000
            || measure > 2
            || [
                grow,
                shrink,
                padding,
                margin,
                gap,
                preferred.width,
                preferred.height,
            ]
            .iter()
            .any(|v| !v.is_finite() || *v < 0.0)
        {
            return Err("Invalid CSS node".into());
        }
        let style = Style {
            display,
            box_sizing: BoxSizing::BorderBox,
            size: Size { width, height },
            min_size: length(0.0),
            flex_direction: direction,
            flex_wrap: wrap,
            flex_basis: basis,
            flex_grow: grow,
            flex_shrink: shrink,
            padding: length(padding),
            margin: length(margin),
            gap: length(gap),
            align_items: Some(AlignItems::STRETCH),
            justify_items: Some(AlignItems::STRETCH),
            align_content: Some(AlignContent::START),
            justify_content: Some(AlignContent::START),
            grid_column: Line {
                start: if column == 0 { auto() } else { line(column) },
                end: span(column_span),
            },
            grid_row: Line {
                start: if row == 0 { auto() } else { line(row) },
                end: span(row_span),
            },
            grid_template_columns: r.tracks(cols)?,
            grid_template_rows: r.tracks(rows)?,
            ..Default::default()
        };
        if measure > 0 {
            measured.push(i);
        }
        nodes.push(Node {
            parent,
            style,
            measure,
            preferred,
        });
    }
    if measured.iter().any(|i| has_children[*i]) {
        return Err("Measured node must be a leaf".into());
    }
    let mut sizes = Vec::with_capacity(frames);
    for _ in 0..frames {
        let size: Size<f32> = Size {
            width: r.number()?,
            height: r.number()?,
        };
        if !size.width.is_finite()
            || size.width < 0.0
            || !size.height.is_finite()
            || (size.height < 0.0 && size.height != -1.0)
        {
            return Err("Invalid viewport".into());
        }
        sizes.push(size);
    }
    if r.0.next().is_some() {
        return Err("Trailing fixture data".into());
    }
    Ok(Fixture {
        nodes,
        sizes,
        measured,
    })
}
#[derive(Clone, Copy)]
struct Metrics {
    kind: u8,
    preferred: Size<f32>,
    revision: usize,
}
fn measure(m: Metrics, known: Size<Option<f32>>, available: Size<AvailableSpace>) -> Size<f32> {
    let preferred = m.preferred.width + (m.revision % 7) as f32 * 8.0;
    let minimum = if m.kind == 2 {
        preferred.min(40.0)
    } else {
        preferred
    };
    let width = known.width.unwrap_or(match available.width {
        AvailableSpace::MinContent => minimum,
        AvailableSpace::MaxContent => preferred,
        AvailableSpace::Definite(limit) => preferred.min(limit),
    });
    let lines = if m.kind == 2 {
        (preferred / width.max(1.0)).ceil().max(1.0)
    } else {
        1.0
    };
    Size {
        width,
        height: known.height.unwrap_or(lines * m.preferred.height),
    }
}
struct Engine<'a> {
    fixture: &'a Fixture,
    tree: TaffyTree<Metrics>,
    nodes: Vec<NodeId>,
    measurements: usize,
}
impl<'a> Engine<'a> {
    fn new(f: &'a Fixture) -> Self {
        let mut tree = TaffyTree::with_capacity(f.nodes.len());
        tree.disable_rounding();
        let mut nodes = Vec::with_capacity(f.nodes.len());
        for n in &f.nodes {
            let node = if n.measure > 0 {
                tree.new_leaf_with_context(
                    n.style.clone(),
                    Metrics {
                        kind: n.measure,
                        preferred: n.preferred,
                        revision: 0,
                    },
                )
            } else {
                tree.new_leaf(n.style.clone())
            }
            .unwrap();
            nodes.push(node);
            if n.parent >= 0 {
                tree.add_child(nodes[n.parent as usize], node).unwrap();
            }
        }
        Self {
            fixture: f,
            tree,
            nodes,
            measurements: 0,
        }
    }
    fn edit(&mut self, i: usize) {
        self.tree
            .get_node_context_mut(self.nodes[i])
            .unwrap()
            .revision += 1;
        self.tree.mark_dirty(self.nodes[i]).unwrap();
    }
    fn calculate(&mut self, size: Size<f32>) {
        self.measurements = 0;
        let count = &mut self.measurements;
        self.tree
            .compute_layout_with_measure(
                self.nodes[0],
                Size {
                    width: AvailableSpace::Definite(size.width),
                    height: if size.height < 0.0 {
                        AvailableSpace::MaxContent
                    } else {
                        AvailableSpace::Definite(size.height)
                    },
                },
                |inputs, _, context, style| {
                    taffy::compute_leaf_layout(
                        inputs,
                        style,
                        |_, _| 0.0,
                        |known, available| {
                            if let Some(context) = context {
                                *count += 1;
                                measure(*context, known, available)
                            } else {
                                Size::ZERO
                            }
                        },
                    )
                },
            )
            .unwrap();
    }
    fn rect(&self, i: usize) -> [f32; 4] {
        let r = self.tree.layout(self.nodes[i]).unwrap();
        let mut x = r.location.x;
        let mut y = r.location.y;
        let mut parent = self.fixture.nodes[i].parent;
        while parent >= 0 {
            let p = self.tree.layout(self.nodes[parent as usize]).unwrap();
            x += p.location.x;
            y += p.location.y;
            parent = self.fixture.nodes[parent as usize].parent;
        }
        [x, y, r.size.width, r.size.height]
    }
    fn consume(&self) -> f64 {
        [0, self.nodes.len() / 2, self.nodes.len() - 1]
            .into_iter()
            .map(|i| {
                let [x, y, w, h] = self.rect(i);
                x as f64 + y as f64 * 0.5 + w as f64 * 0.25 + h as f64 * 0.125
            })
            .sum()
    }
    fn step(&mut self, mode: &str, frame: usize) {
        if mode == "leaf_edit" {
            self.edit(self.fixture.measured[frame * 17 % self.fixture.measured.len()]);
        }
        self.calculate(if mode == "same" || mode == "leaf_edit" {
            *self.fixture.sizes.last().unwrap()
        } else {
            self.fixture.sizes[frame % self.fixture.sizes.len()]
        });
    }
}
fn positive(arg: &str) -> Result<usize> {
    let n = arg.parse().map_err(|_| "Invalid count")?;
    if !(1..=100000).contains(&n) {
        return Err("Invalid count".into());
    }
    Ok(n)
}
fn run() -> Result<()> {
    let args: Vec<_> = env::args().collect();
    if args.len() != 6 || args[1] != "taffy" {
        return Err(
            "Usage: css taffy fixture [verify_]cold|resize|same|leaf_edit|build iterations samples"
                .into(),
        );
    }
    let f = read(&args[2])?;
    let verify = args[3].starts_with("verify_");
    let mode = args[3].strip_prefix("verify_").unwrap_or(&args[3]);
    if !["cold", "resize", "same", "leaf_edit", "build"].contains(&mode) {
        return Err("Unknown mode".into());
    }
    if mode == "leaf_edit" && f.measured.is_empty() {
        return Err("Edit requires measured leaves".into());
    }
    let iterations = positive(&args[4])?;
    let samples = positive(&args[5])?;
    if verify {
        let mut engine = Engine::new(&f);
        if mode != "cold" && mode != "build" {
            engine.calculate(*f.sizes.last().unwrap());
        }
        let mut frames = Vec::with_capacity(iterations);
        for i in 0..iterations {
            if mode == "cold" || mode == "build" {
                engine = Engine::new(&f);
            }
            engine.step(mode, i);
            frames.push(
                (0..f.nodes.len())
                    .map(|j| engine.rect(j))
                    .collect::<Vec<_>>(),
            );
        }
        println!("{{\"frames\":{frames:?}}}");
        return Ok(());
    }
    for size in &f.sizes {
        let mut engine = Engine::new(&f);
        engine.calculate(*size);
    }
    let mut times = Vec::new();
    let mut checksums = Vec::new();
    let mut counts = Vec::new();
    if mode == "build" {
        for _ in 0..samples {
            let mut elapsed = 0.0;
            let mut checksum = 0.0;
            let mut measurements = 0;
            for i in 0..iterations {
                let start = Instant::now();
                let mut engine = Engine::new(&f);
                engine.step(mode, i);
                let consumed = engine.consume();
                elapsed += start.elapsed().as_secs_f64() * 1e6;
                checksum += consumed;
                measurements += engine.measurements;
            }
            times.push(elapsed / iterations as f64);
            checksums.push(checksum);
            counts.push(measurements as f64 / iterations as f64);
        }
    } else {
        for _ in 0..samples {
            let mut engine = Engine::new(&f);
            if mode != "cold" {
                engine.calculate(*f.sizes.last().unwrap());
            }
            let mut elapsed = 0.0;
            let mut checksum = 0.0;
            let mut measurements = 0;
            for i in 0..iterations {
                if mode == "cold" {
                    engine = Engine::new(&f);
                }
                let start = Instant::now();
                engine.step(mode, i);
                let consumed = engine.consume();
                elapsed += start.elapsed().as_secs_f64() * 1e6;
                checksum += consumed;
                measurements += engine.measurements;
            }
            times.push(elapsed / iterations as f64);
            checksums.push(checksum);
            counts.push(measurements as f64 / iterations as f64);
        }
    }
    println!("{{\"build\":\"Release\",\"samples_us\":{times:?},\"checksums\":{checksums:?},\"measurements\":{counts:?}}}");
    Ok(())
}
fn main() {
    if let Err(e) = run() {
        eprintln!("{e}");
        std::process::exit(1);
    }
}
