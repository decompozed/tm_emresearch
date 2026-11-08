using System.Resources;
using System.Text.Json;
using System.Text.RegularExpressions;
using dnlib.DotNet;
using dnlib.DotNet.Emit;
using dnlib.DotNet.Writer;

namespace LauncherRebrand;

record StringRule(string Pattern, string Replace);

record Rules(
    List<StringRule>? Ldstr,
    Dictionary<string, string>? Resources,
    Dictionary<string, Dictionary<string, string>>? Baml,
    string? Audit);

static class Program
{
    static int Main(string[] args)
    {
        if (args.Length != 3)
        {
            Console.Error.WriteLine("usage: LauncherRebrand <in.dll> <out.dll> <rules.json>");
            return 2;
        }
        var rules = JsonSerializer.Deserialize<Rules>(File.ReadAllText(args[2]),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!;
        var audit = new Regex(rules.Audit ?? "$^", RegexOptions.IgnoreCase);
        var report = new Dictionary<string, object>();

        var module = ModuleDefMD.Load(File.ReadAllBytes(args[0]));
        report["ldstr"] = RewriteStrings(module, rules.Ldstr ?? [], audit, out var leftIl);
        report["resources"] = ReplaceResources(module, rules.Resources ?? []);
        report["baml"] = RewriteBaml(module, rules.Baml ?? [], audit, out var leftBaml);
        report["unchanged"] = leftIl.Concat(leftBaml).Distinct().ToList();

        var options = new ModuleWriterOptions(module);
        options.MetadataOptions.Flags |= MetadataFlags.PreserveAll & ~MetadataFlags.PreserveUSOffsets;
        module.Write(args[1], options);
        Console.WriteLine(JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
        return 0;
    }

    static List<object> RewriteStrings(ModuleDef module, List<StringRule> rules, Regex audit, out List<string> left)
    {
        var compiled = rules.Select(r => (Regex: new Regex(r.Pattern), r.Replace)).ToList();
        var changes = new List<object>();
        left = [];
        foreach (var type in module.GetTypes())
            foreach (var method in type.Methods.Where(m => m.HasBody))
                foreach (var ins in method.Body.Instructions.Where(i => i.OpCode == OpCodes.Ldstr))
                {
                    string old = (string)ins.Operand;
                    string value = old;
                    foreach (var (regex, replace) in compiled)
                        value = regex.Replace(value, replace);
                    if (value != old)
                    {
                        ins.Operand = value;
                        changes.Add(new { method = method.FullName, old, @new = value });
                    }
                    else if (audit.IsMatch(old))
                        left.Add(old);
                }
        return changes;
    }

    static List<string> ReplaceResources(ModuleDef module, Dictionary<string, string> map)
    {
        var done = new List<string>();
        foreach (var (name, path) in map)
        {
            int index = module.Resources.ToList().FindIndex(r => r.Name == name);
            if (index < 0 || module.Resources[index] is not EmbeddedResource old)
                throw new InvalidDataException($"embedded resource {name} not found");
            module.Resources[index] = new EmbeddedResource(name, File.ReadAllBytes(path), old.Attributes);
            done.Add(name);
        }
        return done;
    }

    static Dictionary<string, Dictionary<string, int>> RewriteBaml(ModuleDef module,
        Dictionary<string, Dictionary<string, string>> rules, Regex audit, out List<string> left)
    {
        left = [];
        var result = new Dictionary<string, Dictionary<string, int>>();
        int index = module.Resources.ToList().FindIndex(r => r.Name.String.EndsWith(".g.resources"));
        if (index < 0)
            return result;
        var old = (EmbeddedResource)module.Resources[index];
        byte[] data = old.CreateReader().ToArray();
        var entries = new List<(string Name, string Type, byte[] Data)>();
        using (var reader = new ResourceReader(new MemoryStream(data)))
        {
            var e = reader.GetEnumerator();
            while (e.MoveNext())
            {
                string name = (string)e.Key;
                reader.GetResourceData(name, out string type, out byte[] raw);
                entries.Add((name, type, raw));
            }
        }
        foreach (var rule in rules.Keys.Where(k => !entries.Any(e => e.Name == k)))
            throw new InvalidDataException($"{rule} not found in {old.Name}");

        using var output = new MemoryStream();
        using (var writer = new ResourceWriter(output))
        {
            foreach (var (name, type, raw) in entries)
            {
                byte[] value = raw;
                if (name.EndsWith(".baml"))
                {
                    byte[] baml = raw[4..];
                    if (rules.TryGetValue(name, out var map))
                    {
                        var counts = new Dictionary<string, int>();
                        baml = Baml.Replace(baml, map, counts);
                        result[name] = counts;
                        value = [.. BitConverter.GetBytes(baml.Length), .. baml];
                    }
                    left.AddRange(Baml.Strings(baml).Where(s => audit.IsMatch(s)).Select(s => $"{name}: {s}"));
                }
                writer.AddResourceData(name, type, value);
            }
        }
        module.Resources[index] = new EmbeddedResource(old.Name, output.ToArray(), old.Attributes);
        return result;
    }
}
