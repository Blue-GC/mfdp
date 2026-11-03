package pkg.a;

import pkg.b.Repo;

public class Service {
    private final Repo repo = new Repo();

    public String load(int id) {
        return repo.find(id);
    }
}
