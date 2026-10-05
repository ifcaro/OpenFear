#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <iomanip>

// Minimal C++17 test framework, no external dependencies.
//
// Usage:
//   Tests tests;
//   tests.AddCase("Test name", []() {
//       ASSERT_EQ(expected, actual);
//   });
//   int failed = tests.RunAll();

class TestResult {
public:
    std::string name;
    bool passed = false;
    std::string error_message;
    std::chrono::milliseconds duration{0};

    void print() const {
        std::string status = passed ? "PASS" : "FAIL";
        std::cout << "[" << status << "] [" << std::setw(3) << duration.count() << "ms] " << name
                  << "\n";
        if (!passed && !error_message.empty()) {
            std::cout << "       Error: " << error_message << "\n";
        }
    }
};

class TestException : public std::exception {
public:
    explicit TestException(const std::string& msg) : msg_(msg) {}
    const char* what() const noexcept override { return msg_.c_str(); }

private:
    std::string msg_;
};

class Tests {
public:
    void AddCase(const std::string& name, std::function<void()> test_func) {
        cases_.push_back({name, test_func});
    }

    int RunAll() {
        std::cout << "\n";
        std::cout << std::string(80, '=') << "\n";
        std::cout << "Running " << cases_.size() << " test(s)...\n";
        std::cout << std::string(80, '=') << "\n\n";

        std::vector<TestResult> results;
        int passed = 0, failed = 0;

        for (const auto& test : cases_) {
            TestResult result;
            result.name = test.first;

            auto start = std::chrono::high_resolution_clock::now();
            try {
                test.second();
                result.passed = true;
                ++passed;
            } catch (const TestException& e) {
                result.passed = false;
                result.error_message = e.what();
                ++failed;
            } catch (const std::exception& e) {
                result.passed = false;
                result.error_message = std::string("Exception: ") + e.what();
                ++failed;
            } catch (...) {
                result.passed = false;
                result.error_message = "Unknown exception";
                ++failed;
            }
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            results.push_back(result);
            result.print();
        }

        std::cout << "\n" << std::string(80, '=') << "\n";
        std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
        std::cout << std::string(80, '=') << "\n\n";

        return failed;
    }

private:
    std::vector<std::pair<std::string, std::function<void()>>> cases_;
};

// ---------------------------------------------------------------------------
// Assertion macros
// ---------------------------------------------------------------------------

#define ASSERT_EQ(expected, actual)                                                                \
    do {                                                                                           \
        if ((expected) != (actual)) {                                                              \
            throw TestException(std::string(#actual) + " = " + std::to_string(actual) +            \
                                ", expected " + std::to_string(expected));                         \
        }                                                                                          \
    } while (0)

#define ASSERT_NE(a, b)                                                                            \
    do {                                                                                           \
        if ((a) == (b)) {                                                                          \
            throw TestException(std::string(#a) + " == " + #b);                                    \
        }                                                                                          \
    } while (0)

#define ASSERT_TRUE(condition)                                                                     \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            throw TestException(#condition " is false");                                           \
        }                                                                                          \
    } while (0)

#define ASSERT_FALSE(condition)                                                                    \
    do {                                                                                           \
        if ((condition)) {                                                                         \
            throw TestException(#condition " is true");                                            \
        }                                                                                          \
    } while (0)

#define ASSERT_GE(a, b)                                                                            \
    do {                                                                                           \
        if ((a) < (b)) {                                                                           \
            throw TestException(std::string(#a) + " < " + #b);                                     \
        }                                                                                          \
    } while (0)

#define ASSERT_LE(a, b)                                                                            \
    do {                                                                                           \
        if ((a) > (b)) {                                                                           \
            throw TestException(std::string(#a) + " > " + #b);                                     \
        }                                                                                          \
    } while (0)
