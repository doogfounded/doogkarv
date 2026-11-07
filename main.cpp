#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

struct Config {
    std::vector<std::string> files;
    bool char_mode = false;
    std::size_t order = 0;   // 0 means use mode default (2 for words, 4 for chars)
    std::size_t length = 0;  // 0 means use mode default (100 for words, 500 for chars)
    uint64_t seed = 0;
    bool has_seed = false;
};

static void print_help(const char* prog) {
    std::cout
        << "Usage: " << prog << " [files...] [options]\n\n"
        << "A tiny generic Markov chain text generator.\n"
        << "Reads from input file(s), or stdin if no files are given.\n\n"
        << "Options:\n"
        << "  -c, --char         Character-level mode (default: word-level)\n"
        << "  -k, --order <N>    Markov chain order / state size (default: 2 words, 4 chars)\n"
        << "  -n, --length <N>   Number of tokens to generate (default: 100 words, 500 chars)\n"
        << "  -s, --seed <N>     RNG seed for deterministic output\n"
        << "  -h, --help         Show this help message\n";
}

template <typename Token>
struct StateHash {
    std::size_t operator()(const std::vector<Token>& state) const noexcept {
        std::size_t h = 14695981039346656037ULL;
        std::hash<Token> hasher;
        for (const auto& tok : state) {
            h ^= hasher(tok) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        }
        return h;
    }
};

template <typename Token>
class MarkovChain {
public:
    using State = std::vector<Token>;

    explicit MarkovChain(std::size_t order) : order_(order) {}

    void feed(const Token& token) {
        if (window_.size() == order_) {
            State key(window_.begin(), window_.end());
            auto [it, inserted] = transitions_.try_emplace(std::move(key));
            if (inserted) {
                states_.push_back(it->first);
            }
            it->second.push_back(token);
            window_.pop_front();
        }
        window_.push_back(token);
    }

    bool empty() const noexcept {
        return states_.empty();
    }

    std::vector<Token> generate(std::size_t count, std::mt19937_64& rng) const {
        std::vector<Token> output;
        if (count == 0 || states_.empty()) {
            return output;
        }
        output.reserve(count);

        std::uniform_int_distribution<std::size_t> state_dist(0, states_.size() - 1);
        State current = states_[state_dist(rng)];

        for ( const auto& tok : current ) {
            if (output.size() < count) {
                output.push_back(tok);
            }
        }

        while (output.size() < count) {
            auto it = transitions_.find(current);
            if (it == transitions_.end() || it->second.empty()) {
                // Dead-end state: pick a new uniformly random state
                current = states_[state_dist(rng)];
                for (const auto& tok : current) {
                    if (output.size() < count) {
                        output.push_back(tok);
                    }
                }
                continue;
            }

            const auto& next_tokens = it->second;
            std::uniform_int_distribution<std::size_t> next_dist(0, next_tokens.size() - 1);
            const Token& next = next_tokens[next_dist(rng)];
            output.push_back(next);

            if (order_ > 0) {
                current.erase(current.begin());
                current.push_back(next);
            }
        }

        return output;
    }

private:
    std::size_t order_;
    std::deque<Token> window_;
    std::unordered_map<State, std::vector<Token>, StateHash<Token>> transitions_;
    std::vector<State> states_;
};

static bool parse_size(const char* arg, std::size_t& out) {
    try {
        std::size_t pos = 0;
        std::string s(arg);
        unsigned long long val = std::stoull(s, &pos);
        if (pos != s.size() || val == 0) {
            return false;
        }
        out = static_cast<std::size_t>(val);
        return true;
    } catch (...) {
        return false;
    }
}

static bool parse_seed(const char* arg, uint64_t& out) {
    try {
        std::size_t pos = 0;
        std::string s(arg);
        out = static_cast<uint64_t>(std::stoull(s, &pos));
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

static void train_words(std::istream& in, MarkovChain<std::string>& chain) {
    std::string word;
    while (in >> word) {
        chain.feed(word);
    }
}

static void train_chars(std::istream& in, MarkovChain<char>& chain) {
    char ch;
    while (in.get(ch)) {
        if (ch == '\r') {
            if (in.peek() == '\n') {
                continue; // normalize \r\n -> \n
            }
            ch = '\n';
        }
        chain.feed(ch);
    }
}

int main(int argc, char* argv[]) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "-c" || arg == "--char") {
            cfg.char_mode = true;
        } else if (arg == "-k" || arg == "--order") {
            if (++i >= argc || !parse_size(argv[i], cfg.order)) {
                std::cerr << "Error: " << arg << " requires a positive integer.\n";
                return 1;
            }
        } else if (arg == "-n" || arg == "--length") {
            if (++i >= argc || !parse_size(argv[i], cfg.length)) {
                std::cerr << "Error: " << arg << " requires a positive integer.\n";
                return 1;
            }
        } else if (arg == "-s" || arg == "--seed") {
            if (++i >= argc || !parse_seed(argv[i], cfg.seed)) {
                std::cerr << "Error: " << arg << " requires an unsigned integer.\n";
                return 1;
            }
            cfg.has_seed = true;
        } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
            std::cerr << "Error: Unknown option '" << arg << "'. Use --help for usage.\n";
            return 1;
        } else {
            cfg.files.push_back(arg);
        }
    }

    if (cfg.order == 0) {
        cfg.order = cfg.char_mode ? 4 : 2;
    }
    if (cfg.length == 0) {
        cfg.length = cfg.char_mode ? 500 : 100;
    }

    uint64_t seed = cfg.seed;
    if (!cfg.has_seed) {
        std::random_device rd;
        seed = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
    }
    std::mt19937_64 rng(seed);

    auto process_inputs = [&](auto&& trainer) -> bool {
        if (cfg.files.empty()) {
            trainer(std::cin);
            return true;
        }
        for (const auto& path : cfg.files) {
            if (path == "-") {
                trainer(std::cin);
            } else {
                std::ifstream file(path, std::ios::binary);
                if (!file) {
                    std::cerr << "Error: Could not open file '" << path << "'.\n";
                    return false;
                }
                trainer(file);
            }
        }
        return true;
    };

    if (cfg.char_mode) {
        MarkovChain<char> chain(cfg.order);
        if (!process_inputs([&](std::istream& in) { train_chars(in, chain); })) {
            return 1;
        }
        if (chain.empty()) {
            std::cerr << "Error: Input contains fewer characters than order (" << cfg.order + 1 << " required).\n";
            return 1;
        }
        auto out = chain.generate(cfg.length, rng);
        std::cout.write(out.data(), static_cast<std::streamsize>(out.size()));
        if (out.empty() || out.back() != '\n') {
            std::cout << '\n';
        }
    } else {
        MarkovChain<std::string> chain(cfg.order);
        if (!process_inputs([&](std::istream& in) { train_words(in, chain); })) {
            return 1;
        }
        if (chain.empty()) {
            std::cerr << "Error: Input contains fewer words than order (" << cfg.order + 1 << " required).\n";
            return 1;
        }
        auto out = chain.generate(cfg.length, rng);
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (i > 0) {
                std::cout << ' ';
            }
            std::cout << out[i];
        }
        std::cout << '\n';
    }

    return 0;
}

