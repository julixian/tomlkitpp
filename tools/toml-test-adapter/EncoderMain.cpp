import std;
import tomlkitpp;
import TaggedJson;

int main(int argc, char** argv)
{
    tomlkit::FormatOptions opts{};
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "-toml=1.0" || arg == "--v1.0") {
            opts.spec = tomlkit::SpecVersion::v1_0;
        }
    }

    const std::string input(
        (std::istreambuf_iterator<char>(std::cin)),
        std::istreambuf_iterator<char>()
    );

    try {
        const tomlkit::Item doc = tomlkit::test_adapter::taggedJsonToItem(input);
        std::print("{}", doc.toToml(opts));
        return 0;
    }
    catch (const std::exception& ex) {
        std::println(std::cerr, "Encoder error: {}", ex.what());
        return 1;
    }
}
