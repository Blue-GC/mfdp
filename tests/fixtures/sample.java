package demo;

import java.util.ArrayList;
import java.util.List;

public class Inventory extends Base implements Comparable<Inventory> {
    private final List<String> items = new ArrayList<>();
    public static final int LIMIT = 10;

    public Inventory() {}

    /** Add an item. */
    public void add(String item, int... extra) {
        validate(item);
        items.add(item);
    }

    private void validate(String item) {
        if (item == null) {
            throw new IllegalArgumentException("null");
        }
    }

    public int size() {
        return items.size() + LIMIT;
    }

    public enum Level { LOW, HIGH }
}
