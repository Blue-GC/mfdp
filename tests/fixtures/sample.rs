use std::collections::HashMap;

pub struct Inventory {
    items: HashMap<String, u32>,
}

impl Inventory {
    pub fn new() -> Self {
        Inventory { items: HashMap::new() }
    }

    /// Add stock.
    pub fn add(&mut self, sku: &str, qty: u32) {
        self.validate(sku);
        *self.items.entry(sku.to_string()).or_insert(0) += qty;
    }

    fn validate(&self, sku: &str) {
        assert!(!sku.is_empty());
    }
}

pub trait Describe {
    fn describe(&self) -> String;
}

impl Describe for Inventory {
    fn describe(&self) -> String {
        format!("{} items", self.items.len())
    }
}

pub const MAX_ITEMS: usize = 100;

pub fn total(inv: &Inventory) -> usize {
    inv.items.len()
}
