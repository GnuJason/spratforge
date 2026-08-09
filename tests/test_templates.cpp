#include <cassert>
#include <filesystem>

#include "templates/template_engine.hpp"

int main() {
    const auto directory = std::filesystem::path(__FILE__).parent_path().parent_path() / "profiles/templates";
    const auto templates = spratforge::templates::load_template_registry(directory.string());
    const auto repeated = spratforge::templates::load_template_registry(directory.string());
    assert(templates.size() == 10U && repeated.size() == templates.size());
    for (std::size_t index = 0; index < templates.size(); ++index) {
        assert(templates[index].name == repeated[index].name);
        assert(templates[index].motion.dx == repeated[index].motion.dx);
        assert(templates[index].motion.dy == repeated[index].motion.dy);
    }
    const auto* idle = spratforge::templates::find_template(templates, "idle");
    assert(idle != nullptr && idle->frame_count == 1 && idle->fps == 8);
    return 0;
}
