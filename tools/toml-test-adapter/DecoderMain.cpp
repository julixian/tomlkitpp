import std;
import tomlkitpp;
import TaggedJson;

int main(int argc, char** argv)
{
    tomlkit::ParseOptions opts{};
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "-toml=1.0" || arg == "--v1.0") {
            opts.spec = tomlkit::SpecVersion::v1_0;
        }
        else if (arg == "-toml=1.1" || arg == "--v1.1") {
            opts.spec = tomlkit::SpecVersion::v1_1;
        }
    }

    const std::string input(
        (std::istreambuf_iterator<char>(std::cin)),
        std::istreambuf_iterator<char>()
    );

    const auto res = tomlkit::tryParse(input, opts, "<stdin>");
    if (!res) {
        std::println(std::cerr, "{}", res.error().format());
        return 1;
    }

    std::println("{}", tomlkit::test_adapter::itemToTaggedJson(*res));
    return 0;
}
