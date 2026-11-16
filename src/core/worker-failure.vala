namespace ImageKit {
    internal class WorkerFailure : Object {
        public static DecodeFailure classify (Error error) {
            if (error is ImageError.UNSUPPORTED || error is ImageError.RAW_UNSUPPORTED) {
                return DecodeFailure.UNSUPPORTED;
            }
            if (error is ImageError.CODEC_MISSING) {
                return DecodeFailure.CODEC_MISSING;
            }
            if (error is ImageError.CORRUPT) {
                return DecodeFailure.CORRUPT;
            }
            if (error is ImageError.DECODER_CRASH) {
                return DecodeFailure.CRASH;
            }
            if (error is ImageError.MEMORY) {
                return DecodeFailure.MEMORY;
            }
            if (error is ImageError.COLOR_PROFILE) {
                return DecodeFailure.COLOR_PROFILE;
            }
            if (error is IOError.TIMED_OUT) {
                return DecodeFailure.TIMEOUT;
            }
            return DecodeFailure.FAILURE;
        }

        public static string serialize (Error error) {
            string message = error.message.make_valid ();
            string lower = message.down ();
            string kind = "decoder-failure";
            if (error is ImageError.CORRUPT) {
                kind = "corrupt";
            } else if (error is ImageError.UNSUPPORTED) {
                kind = "unsupported";
            } else if (error is ImageError.CODEC_MISSING) {
                kind = "missing-codec";
            } else if (error is ImageError.RAW_UNSUPPORTED) {
                kind = "raw-unsupported";
            } else if (error is ImageError.COLOR_PROFILE || lower.contains ("profile")) {
                kind = "colour-profile";
            } else if (error is ImageError.MEMORY || lower.contains ("memory") ||
                lower.contains ("allocation")) {
                kind = "memory";
            } else if (lower.contains ("codec") || lower.contains ("compression not supported") ||
                lower.contains ("unsupported file-type")) {
                kind = "missing-codec";
            } else if (lower.contains ("not a known file format") || lower.contains ("unsupported format")) {
                kind = "unsupported";
            } else if (lower.contains ("corrupt") || lower.contains ("truncated") ||
                lower.contains ("premature") || lower.contains ("invalid")) {
                kind = "corrupt";
            }
            if (message.length > 2000) {
                message = message.substring (0, 2000).make_valid ();
            }
            var object = new Json.Object ();
            object.set_string_member ("kind", kind);
            object.set_string_member ("message", message.replace ("/source", _("image")));
            var node = new Json.Node (Json.NodeType.OBJECT);
            node.set_object (object);
            var generator = new Json.Generator ();
            generator.set_root (node);
            return generator.to_data (null);
        }

        public static Error deserialize (string diagnostic) {
            try {
                string text = diagnostic.strip ();
                int last = text.last_index_of ("\n{");
                if (last >= 0) {
                    text = text.substring (last+1);
                }
                var parser = new Json.Parser ();
                parser.load_from_data (text);
                if (parser.get_root () == null || parser.get_root ().get_node_type () != Json.NodeType.OBJECT) {
                    throw new IOError.INVALID_DATA ("Invalid decoder diagnostic");
                }
                var root = parser.get_root ().get_object ();
                if (!root.has_member ("message") || !root.has_member ("kind") ||
                    root.get_member ("message").get_value_type () != typeof (string) ||
                    root.get_member ("kind").get_value_type () != typeof (string)) {
                    throw new IOError.INVALID_DATA ("Invalid decoder diagnostic");
                }
                string message = root.get_string_member ("message");
                switch (root.get_string_member ("kind")) {
                    case "raw-unsupported":
                        return new ImageError.RAW_UNSUPPORTED ("%s", message);
                    case "unsupported":
                        return new ImageError.UNSUPPORTED ("%s", message);
                    case "missing-codec":
                        return new ImageError.CODEC_MISSING ("%s", message);
                    case "corrupt":
                        return new ImageError.CORRUPT ("%s", message);
                    case "memory":
                        return new ImageError.MEMORY ("%s", message);
                    case "colour-profile":
                        return new ImageError.COLOR_PROFILE ("%s", message);
                    default:
                        return new ImageError.DECODE ("%s", message);
                }
            } catch (Error e) {
                return new ImageError.DECODE (_("The decoder could not read this image. Check the file or the installed codecs."));
            }
        }
    }
}
