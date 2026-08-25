# Contributing to MindStudio Monitor

<!-- md-trans-meta sourceCommit=4222e88544e08580745f0b6eec2d1d22cda8f31b translatedAt=2026-08-12T06:43:23.861Z pushedAt=2026-08-12T06:43:51.961Z -->

Thank you for considering contributing to MindStudio Monitor (msMonitor)! We welcome contributions of all kinds, including bug fixes, feature enhancements, documentation improvements, and even just feedback. Whether you are an experienced developer or participating in an open source project for the first time, your help is invaluable.

You can support this project in several ways:

- Report issues through [Issues](https://gitcode.com/Ascend/msmonitor/issues).

- Suggest or implement new features.

- Improve or expand the documentation.

- Review Pull Requests and assist other contributors.

- Promote the project: share msMonitor in blog posts, on social media, or give the repository a ⭐.

## Finding Issues to Contribute To

Check out the following types of issues:

- Good first issues

- Call for contribution

In addition, you can learn about the project's development plans and roadmap by viewing the [Issue list](https://gitcode.com/Ascend/msmonitor/issues).

## Contribution Process

### Environment Requirements

- For the hardware environment, see [Ascend Product Form Description](https://www.hiascend.com/document/detail/en/AscendFAQ/ProduTech/productform/hardwaredesc_0001.html)

- The open-source version of CANN must be installed in advance.

- Python 3.8 or later

- CMake 3.14 or later

### Development and Testing

1. Fork a copy of the source code to your personal repository for code development.

   ```bash
   git clone https://gitcode.com/<your-username>/msmonitor.git
   cd msmonitor
   ```

2. Perform code development in your personal repository.

   For code development, follow the [Coding Standards](#coding-standards).

3. Test your code.

   See [Code Testing](#code-testing).

4. Test the compiled package.

   Compile the developed code into a .whl package for testing. For detailed steps, see [msMonitor Tool Installation Guide](../install_guide/msmonitor_install_guide.md).

5. Develop the documentation.

   If new, changed, or deleted features are involved, provide relevant documentation. For detailed documentation writing requirements, see [Documentation Development](#documentation-development).

6. Submit a PR.

   See <a href="#pr-submission-process">Pull Request Process</a>.

### Coding Standards

#### Python Coding Standards

- Follow the PEP 8 coding standard.

- Use 4 spaces for indentation.

- Use PascalCase for class names (e.g., `DataManager`).

- Use snake_case for function and variable names (e.g., `parse_data`).

- Add necessary type annotations and docstrings.

#### C++ Coding Standards

- Follow the existing coding style of the project.

- Use 4 spaces for indentation.

- Use PascalCase for class names.

- Use camelCase for function names.

- Add necessary comments to explain complex logic.

### Code Testing

#### Running Tests

Before submitting code, ensure that all tests pass.

```bash
bash scripts/run_ut.sh
bash scripts/run_st.sh
```

#### Adding Tests

- Add corresponding unit tests for new features.

- Ensure that tests cover the main logical branches.

- Test cases should be readable and maintainable.

- Test data should be placed in the appropriate location under the `test/` directory.

### Documentation Development

#### Documentation Paths

If your changes affect how users interact with the product, update the relevant documentation:

- User guide: `docs/en/`

- API Documentation: docstrings in code comments

- Sample code: `samples/`

#### Documentation Conventions

- Use clear and concise expressions.

- Provide complete sample code.

- Include necessary screenshots or diagrams for illustration.

- Ensure link validity.

### PR Submission Process

#### Pre-Submission Checklist

Before submitting a PR, ensure the following:

- [ ] Code adheres to the project's Coding Standards.

- [ ] Necessary test cases have been added.

- [ ] All tests pass.

- [ ] Documentation is updated.

- [ ] Commit message is clear and explicit.

- [ ] Code has been self-reviewed.

#### Submission Process

1. **Create a branch.**

   ```bash
   git checkout -b feature/<your-feature-name>
   ```

2. **Commit changes.**

   ```bash
   git add .
   git commit -m "feat: <your feature description>"
   ```

3. **Push to remote repository.**

   ```bash
   git push origin feature/<your-feature-name>
   ```

4. **Create a PR**.

   Create a PR on GitCode and fill in the following:

   1. A clear title

      Follow the [Commit Message](#commit-message-specification) specification.

   2. A detailed description

      Include the changes made, reasons, test results, etc.

   3. Links the related Issue

5. **Review the code.**

   1. After submitting a PR, notify the relevant Reviewers and Committers to review the content.

   2. You need to modify the code based on the review feedback and resubmit the update. This process may of multiple rounds, so remain responsive and communicative.

   The PR process will prompt the relevant Reviewers and Committers. You can specify the relevant Reviewers and Committers during the PR process, or contact us via [README](../../../README_EN.md#-suggestions-and-communication).

6. **Merge the code.**

   The PR must be attached with the following four Labels in sequence to complete code merging:

   1. ascend-cla/yes: CLA check. You need to sign the CLA when contributing for the first time. After signing, this Label is automatically obtained for each subsequent submission.

   2. ci-pipeline-passed: CI pipeline. Trigger it by commenting `compile` in the PR process. If the CI pipeline check fails, modify the code based on the prompts and resubmit.

   3. lgtm: Provided by Reviewers. After the Reviewers approve the review, they comment `/lgtm` in the PR process to trigger the lgtm Label.

   4. approved: Provided by Committers. After Committers approve, they comment `/approved` in the PR process to trigger the approved Label.

   When your PR has all four Labels, your PR will be merged into the main branch.

#### PR Best Practices

- Keep PRs at a manageable size for easier review.

- Each PR should address only one issue or implement one feature.

- Respond to review comments in a timely manner.

- Stay synchronized with the main branch and resolve conflicts promptly.

#### Commit Message Specification

The commit message should clearly describe what was changed and why:

```text
<type>: <subject>

<body>

<footer>
```

The types include:

- `feat`: New feature

- `fix`: Bug fix

- `docs`: Documentation updates

- `style`: Code formatting adjustments (no functional impact)

- `refactor`: Code refactoring

- `test`: Test-related changes

- `chore`: Changes to the build process or auxiliary tools

Example:

```text
feat: add memory usage analysis

- Implement memory data collection module
- Add memory usage trend analysis algorithm
- Update related documentation

Closes #123
```

## Community Guidelines

### Code of Conduct

We are committed to providing a friendly, safe, and inclusive environment for all participants. By participating in this project, you agree to:

- Respect differing viewpoints and experiences.
- Accept constructive criticism.
- Focus on what is best for the community.
- Show empathy towards other community members.

### Communication Channels

- **Issues**: Used for reporting bugs, proposing feature suggestions, and discussing technical issues.

- **PRs**: Used for code review and discussing specific implementations.

- **WeChat Group**: For daily communication and quick Q&A (see [README](../../../README_EN.md#-suggestions-and-communication)).

## License

By contributing code to this project, you agree that your contributions will be licensed under the project's license. For details, see the [LICENSE](../../../LICENSE) file.

The documentation in the docs directory of the msMonitor tool is licensed under the CC-BY 4.0 license. For details, see [docs/LICENSE](../../LICENSE).

## Acknowledgments

Thank you for contributing to msMonitor. Your efforts make this project stronger and more user-friendly. We look forward to your participation!

If you have any questions or need assistance, feel free to ask in [Issues](https://gitcode.com/Ascend/msmonitor/issues) or contact us through other community channels.
