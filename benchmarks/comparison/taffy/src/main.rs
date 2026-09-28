use std::{env, fs, str::SplitWhitespace, time::Instant};
use taffy::prelude::*;

type Result<T> = std::result::Result<T, String>;
struct Reader<'a>(SplitWhitespace<'a>);
impl Reader<'_> {
    fn next(&mut self) -> Result<&str> {
        self.0.next().ok_or_else(|| "Truncated fixture".into())
    }
    fn number<T: std::str::FromStr>(&mut self) -> Result<T> {
        self.next()?.parse().map_err(|_| "Invalid number".into())
    }
    fn tracks(&mut self, count: usize) -> Result<Vec<GridTemplateComponent<String>>> {
        (0..count)
            .map(|_| {
                let kind = self.next()?.to_owned();
                let value: f32 = self.number()?;
                if !value.is_finite() || value < 0.0 {
                    return Err("Invalid track".into());
                }
                match kind.as_str() {
                    "F" => Ok(length(value)),
                    "W" if value >= 1.0 => Ok(minmax(length(0.0), fr(value))),
                    "C" => Ok(minmax(min_content(), max_content())),
                    _ => Err("Unsupported track".into()),
                }
            })
            .collect()
    }
}
struct Item {
    column: i16,
    row: i16,
    column_span: u16,
    row_span: u16,
    preferred: Size<f32>,
}
struct Fixture {
    columns: Vec<GridTemplateComponent<String>>,
    rows: Vec<GridTemplateComponent<String>>,
    items: Vec<Item>,
    sizes: Vec<Size<f32>>,
    measured: bool,
    padding: f32,
    gap: f32,
}
fn read(path: &str) -> Result<Fixture> {
    let text = fs::read_to_string(path).map_err(|e| e.to_string())?;
    let mut r = Reader(text.split_whitespace());
    if r.next()? != "GLCMP1" {
        return Err("Invalid fixture magic".into());
    }
    let _name = r.next()?;
    let kind = r.next()?.to_owned();
    let cols: usize = r.number()?;
    let rows: usize = r.number()?;
    let count: usize = r.number()?;
    let frames: usize = r.number()?;
    if !(1..=1000).contains(&cols)
        || !(1..=1000).contains(&rows)
        || !(1..=100000).contains(&count)
        || !(2..=100).contains(&frames)
        || !["row", "column", "grid"].contains(&kind.as_str())
    {
        return Err("Invalid fixture shape".into());
    }
    let measured: u8 = r.number()?;
    let padding = r.number()?;
    let gap = r.number()?;
    let columns = r.tracks(cols)?;
    let row_tracks = r.tracks(rows)?;
    let mut items = Vec::with_capacity(count);
    for _ in 0..count {
        let column: i16 = r.number()?;
        let row: i16 = r.number()?;
        let column_span: u16 = r.number()?;
        let row_span: u16 = r.number()?;
        if column < 0
            || row < 0
            || column_span == 0
            || row_span == 0
            || column as usize + column_span as usize > cols
            || row as usize + row_span as usize > rows
        {
            return Err("Invalid placement".into());
        }
        let preferred = Size {
            width: r.number()?,
            height: r.number()?,
        };
        items.push(Item {
            column,
            row,
            column_span,
            row_span,
            preferred,
        });
    }
    let mut sizes = Vec::with_capacity(frames);
    for _ in 0..frames {
        sizes.push(Size {
            width: r.number()?,
            height: r.number()?,
        });
    }
    if r.0.next().is_some() {
        return Err("Trailing fixture data".into());
    }
    Ok(Fixture {
        columns,
        rows: row_tracks,
        items,
        sizes,
        measured: measured != 0,
        padding,
        gap,
    })
}
struct Engine {
    tree: TaffyTree<Size<f32>>,
    root: NodeId,
    children: Vec<NodeId>,
    measured: bool,
    measurements: usize,
}
impl Engine {
    fn new(f: &Fixture) -> Self {
        let mut tree = TaffyTree::with_capacity(f.items.len() + 1);
        tree.disable_rounding();
        let mut children = Vec::with_capacity(f.items.len());
        for item in &f.items {
            let style = Style {
                grid_column: Line {
                    start: line(item.column + 1),
                    end: span(item.column_span),
                },
                grid_row: Line {
                    start: line(item.row + 1),
                    end: span(item.row_span),
                },
                min_size: length(0.0),
                justify_self: Some(AlignItems::STRETCH),
                align_self: Some(AlignItems::STRETCH),
                ..Default::default()
            };
            let node = if f.measured {
                tree.new_leaf_with_context(style, item.preferred).unwrap()
            } else {
                tree.new_leaf(style).unwrap()
            };
            children.push(node);
        }
        let root = tree
            .new_with_children(
                Style {
                    display: Display::Grid,
                    box_sizing: BoxSizing::BorderBox,
                    size: percent(1.0),
                    min_size: length(0.0),
                    grid_template_columns: f.columns.clone(),
                    grid_template_rows: f.rows.clone(),
                    padding: length(f.padding),
                    gap: length(f.gap),
                    justify_content: Some(AlignContent::START),
                    align_content: Some(AlignContent::START),
                    ..Default::default()
                },
                &children,
            )
            .unwrap();
        Self {
            tree,
            root,
            children,
            measured: f.measured,
            measurements: 0,
        }
    }
    fn calculate(&mut self, size: Size<f32>) {
        self.measurements = 0;
        let available = Size {
            width: AvailableSpace::Definite(size.width),
            height: AvailableSpace::Definite(size.height),
        };
        if self.measured {
            let count = &mut self.measurements;
            self.tree
                .compute_layout_with_measure(self.root, available, |inputs, _, context, style| {
                    let metrics = context.copied().unwrap_or(Size::ZERO);
                    taffy::compute_leaf_layout(
                        inputs,
                        style,
                        |_, _| 0.0,
                        |known, _| {
                            *count += 1;
                            Size {
                                width: known.width.unwrap_or(metrics.width),
                                height: known.height.unwrap_or(metrics.height),
                            }
                        },
                    )
                })
                .unwrap();
        } else {
            self.tree.compute_layout(self.root, available).unwrap();
        }
    }
    fn rect(&self, i: usize) -> [f64; 4] {
        let r = self.tree.layout(self.children[i]).unwrap();
        [
            r.location.x as f64,
            r.location.y as f64,
            r.size.width as f64,
            r.size.height as f64,
        ]
    }
    fn consume(&self) -> f64 {
        [0, self.children.len() / 2, self.children.len() - 1]
            .into_iter()
            .map(|i| {
                let [x, y, w, h] = self.rect(i);
                x + y * 0.5 + w * 0.25 + h * 0.125
            })
            .sum()
    }
}
fn positive(arg: &str) -> Result<usize> {
    let value = arg.parse().map_err(|_| "Invalid count")?;
    if !(1..=1000000).contains(&value) {
        return Err("Invalid count".into());
    }
    Ok(value)
}
fn run() -> Result<()> {
    let args: Vec<_> = env::args().collect();
    if args.len() != 6 || args[1] != "taffy" {
        return Err("Usage: taffy fixture verify|cold|resize|same iterations samples".into());
    }
    let f = read(&args[2])?;
    let mode = args[3].as_str();
    if !["verify", "cold", "resize", "same"].contains(&mode) {
        return Err("Unknown mode".into());
    }
    let iterations = positive(&args[4])?;
    let samples = positive(&args[5])?;
    if mode == "verify" {
        let mut engine = Engine::new(&f);
        let mut frames = Vec::new();
        for i in 0..f.sizes.len() * 2 {
            if i >= f.sizes.len() {
                engine = Engine::new(&f);
            }
            engine.calculate(f.sizes[i % f.sizes.len()]);
            frames.push(
                (0..f.items.len())
                    .map(|j| engine.rect(j))
                    .collect::<Vec<_>>(),
            );
        }
        println!("{{\"frames\":{frames:?}}}");
        return Ok(());
    }
    for size in &f.sizes {
        let mut e = Engine::new(&f);
        e.calculate(*size);
    }
    let mut times = Vec::new();
    let mut checksums = Vec::new();
    let mut counts = Vec::new();
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
            let size = if mode != "same" {
                f.sizes[i % f.sizes.len()]
            } else {
                *f.sizes.last().unwrap()
            };
            let start = Instant::now();
            engine.calculate(size);
            let consumed = engine.consume();
            let stop = Instant::now();
            elapsed += stop.duration_since(start).as_secs_f64() * 1e6;
            checksum += consumed;
            measurements += engine.measurements;
        }
        times.push(elapsed / iterations as f64);
        checksums.push(checksum);
        counts.push(measurements as f64 / iterations as f64);
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
