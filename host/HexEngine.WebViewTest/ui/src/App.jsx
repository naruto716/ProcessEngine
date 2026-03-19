import { useEffect, useMemo, useState } from "react";
import {
  Alert,
  Box,
  Button,
  Card,
  CardContent,
  Chip,
  Container,
  CssBaseline,
  Divider,
  Stack,
  ThemeProvider,
  Typography,
  createTheme,
} from "@mui/material";
import AutoAwesomeIcon from "@mui/icons-material/AutoAwesome";
import BoltIcon from "@mui/icons-material/Bolt";
import CableIcon from "@mui/icons-material/Cable";
import DataObjectIcon from "@mui/icons-material/DataObject";
import MemoryIcon from "@mui/icons-material/Memory";
import RefreshIcon from "@mui/icons-material/Refresh";

const theme = createTheme({
  palette: {
    mode: "dark",
    primary: {
      main: "#f3b562",
    },
    secondary: {
      main: "#59d1c2",
    },
    background: {
      default: "#08111d",
      paper: "#0f1c2c",
    },
    text: {
      primary: "#f4f8ff",
      secondary: "#91a7c4",
    },
  },
  typography: {
    fontFamily: '"Bahnschrift", "Segoe UI Variable Display", "Segoe UI", sans-serif',
    h2: {
      fontWeight: 700,
      letterSpacing: "-0.03em",
    },
    h5: {
      fontWeight: 700,
    },
    button: {
      textTransform: "none",
      fontWeight: 700,
    },
  },
  shape: {
    borderRadius: 18,
  },
  components: {
    MuiCard: {
      styleOverrides: {
        root: {
          background: "linear-gradient(180deg, rgba(15,28,44,0.92), rgba(10,18,31,0.94))",
          border: "1px solid rgba(141, 166, 196, 0.14)",
          boxShadow: "0 30px 80px rgba(0, 0, 0, 0.28)",
          backdropFilter: "blur(18px)",
        },
      },
    },
    MuiButton: {
      styleOverrides: {
        root: {
          borderRadius: 14,
          paddingInline: 16,
          paddingBlock: 10,
        },
      },
    },
  },
});

function eventSummary(payload) {
  if (payload == null) {
    return "null";
  }

  if (typeof payload === "string") {
    return payload;
  }

  if (payload.values && Array.isArray(payload.values.values)) {
    return payload.values.values.map((value) => JSON.stringify(value)).join(", ");
  }

  return JSON.stringify(payload, null, 2);
}

export default function App() {
  const [events, setEvents] = useState([]);
  const [hostReady, setHostReady] = useState(false);
  const [bridgeReady, setBridgeReady] = useState(Boolean(window.chrome?.webview));

  useEffect(() => {
    if (!window.chrome?.webview) {
      return undefined;
    }

    const handler = (event) => {
      const envelope = {
        id: crypto.randomUUID(),
        time: new Date().toLocaleTimeString(),
        type: event.data.type,
        payload: event.data.payload,
      };

      if (event.data.type === "host-ready") {
        setHostReady(true);
      }

      setEvents((current) => [envelope, ...current].slice(0, 20));
    };

    window.chrome.webview.addEventListener("message", handler);
    return () => window.chrome.webview.removeEventListener("message", handler);
  }, []);

  const lastEvent = events[0];

  const stats = useMemo(
    () => [
      { label: "Bridge", value: bridgeReady ? "connected" : "missing", color: bridgeReady ? "success" : "error" },
      { label: "Host", value: hostReady ? "ready" : "booting", color: hostReady ? "secondary" : "warning" },
      { label: "Events", value: String(events.length), color: "default" },
    ],
    [bridgeReady, events.length, hostReady],
  );

  const post = (action, payload = null) => {
    if (!window.chrome?.webview) {
      setEvents((current) => [
        {
          id: crypto.randomUUID(),
          time: new Date().toLocaleTimeString(),
          type: "error",
          payload: { message: "window.chrome.webview is unavailable" },
        },
        ...current,
      ]);
      return;
    }

    window.chrome.webview.postMessage({ action, payload });
  };

  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <Box
        sx={{
          minHeight: "100vh",
          background:
            "radial-gradient(circle at top left, rgba(89,209,194,0.16), transparent 28%), radial-gradient(circle at bottom right, rgba(243,181,98,0.16), transparent 30%), linear-gradient(135deg, #060d17, #0a1320 52%, #09111d)",
          py: 4,
        }}
      >
        <Container maxWidth="xl">
          <Stack spacing={3}>
            <Card>
              <CardContent sx={{ p: 4 }}>
                <Box
                  sx={{
                    display: "grid",
                    gap: 3,
                    gridTemplateColumns: { xs: "1fr", md: "minmax(0, 1.8fr) minmax(320px, 1fr)" },
                    alignItems: "center",
                  }}
                >
                  <Box>
                    <Stack spacing={2}>
                      <Chip
                        icon={<AutoAwesomeIcon />}
                        label="WPF shell + WebView2 + native C++ bridge"
                        sx={{ alignSelf: "flex-start", bgcolor: "rgba(243,181,98,0.12)", color: "#ffd8a7" }}
                      />
                      <Typography variant="h2">HexEngine desktop shell</Typography>
                      <Typography variant="body1" color="text.secondary" sx={{ maxWidth: 760, lineHeight: 1.8 }}>
                        This UI is served locally inside WebView2, talks to the WPF host over the message bridge,
                        and routes real work through the native C++ engine. The outer window chrome stays native;
                        the content surface is React + MUI.
                      </Typography>
                      <Stack direction="row" spacing={1.5} flexWrap="wrap" useFlexGap>
                        {stats.map((stat) => (
                          <Chip
                            key={stat.label}
                            label={`${stat.label}: ${stat.value}`}
                            color={stat.color}
                            variant={stat.color === "default" ? "outlined" : "filled"}
                          />
                        ))}
                      </Stack>
                    </Stack>
                  </Box>

                  <Box>
                    <Card sx={{ background: "linear-gradient(160deg, rgba(89,209,194,0.14), rgba(15,28,44,0.92))" }}>
                      <CardContent>
                        <Stack spacing={1.5}>
                          <Stack direction="row" spacing={1.5} alignItems="center">
                            <MemoryIcon color="secondary" />
                            <Typography variant="h5">Live native bridge</Typography>
                          </Stack>
                          <Typography variant="body2" color="text.secondary" sx={{ lineHeight: 1.8 }}>
                            Use the actions below to prove the JS -&gt; C# -&gt; native DLL -&gt; LuaRuntime path is active.
                          </Typography>
                          <Typography variant="body2" color="secondary.main">
                            The counter action persists in a real native script environment.
                          </Typography>
                        </Stack>
                      </CardContent>
                    </Card>
                  </Box>
                </Box>
              </CardContent>
            </Card>

            <Box
              sx={{
                display: "grid",
                gap: 3,
                gridTemplateColumns: { xs: "1fr", lg: "minmax(0, 1.35fr) minmax(320px, 1fr)" },
              }}
            >
              <Box>
                <Card>
                  <CardContent sx={{ p: 3 }}>
                    <Stack spacing={2.5}>
                      <Stack direction="row" spacing={1.5} alignItems="center">
                        <BoltIcon color="primary" />
                        <Typography variant="h5">Host actions</Typography>
                      </Stack>
                      <Box
                        sx={{
                          display: "grid",
                          gap: 1.5,
                          gridTemplateColumns: { xs: "1fr", sm: "repeat(2, minmax(0, 1fr))" },
                        }}
                      >
                        <Box>
                          <Button fullWidth variant="contained" onClick={() => post("runNativeGlobal")}>
                            Run native Lua demo
                          </Button>
                        </Box>
                        <Box>
                          <Button fullWidth variant="contained" color="secondary" onClick={() => post("runNativeScriptCounter")}>
                            Increment native counter
                          </Button>
                        </Box>
                        <Box>
                          <Button fullWidth variant="outlined" onClick={() => post("resetNativeScriptCounter")}>
                            Reset native counter
                          </Button>
                        </Box>
                        <Box>
                          <Button fullWidth variant="outlined" onClick={() => post("getHostInfo")}>
                            Get host info
                          </Button>
                        </Box>
                        <Box>
                          <Button fullWidth variant="text" onClick={() => post("ping")}>
                            Ping host
                          </Button>
                        </Box>
                        <Box>
                          <Button fullWidth variant="text" onClick={() => setEvents([])} startIcon={<RefreshIcon />}>
                            Clear event log
                          </Button>
                        </Box>
                      </Box>

                      {lastEvent ? (
                        <Alert
                          icon={<CableIcon />}
                          severity={lastEvent.type === "error" ? "error" : "info"}
                          sx={{ borderRadius: 3 }}
                        >
                          <Typography variant="subtitle2">{lastEvent.type}</Typography>
                          <Typography variant="body2" sx={{ whiteSpace: "pre-wrap", mt: 0.5 }}>
                            {eventSummary(lastEvent.payload)}
                          </Typography>
                        </Alert>
                      ) : (
                        <Alert icon={<DataObjectIcon />} severity="info" sx={{ borderRadius: 3 }}>
                          No host events yet. Use the actions above to populate the live event stream.
                        </Alert>
                      )}
                    </Stack>
                  </CardContent>
                </Card>
              </Box>

              <Box>
                <Card sx={{ height: "100%" }}>
                  <CardContent sx={{ p: 0 }}>
                    <Stack direction="row" justifyContent="space-between" alignItems="center" sx={{ px: 3, py: 2.5 }}>
                      <Typography variant="h5">Event stream</Typography>
                      <Chip label={`${events.length} entries`} variant="outlined" />
                    </Stack>
                    <Divider />
                    <Stack spacing={0} sx={{ maxHeight: 560, overflow: "auto" }}>
                      {events.length === 0 ? (
                        <Box sx={{ p: 3 }}>
                          <Typography color="text.secondary">
                            The host will push JSON envelopes back into this view after each native or shell action.
                          </Typography>
                        </Box>
                      ) : (
                        events.map((event) => (
                          <Box key={event.id} sx={{ px: 3, py: 2.5 }}>
                            <Stack direction="row" justifyContent="space-between" spacing={2}>
                              <Typography variant="subtitle1" sx={{ fontWeight: 700 }}>
                                {event.type}
                              </Typography>
                              <Typography variant="caption" color="text.secondary">
                                {event.time}
                              </Typography>
                            </Stack>
                            <Typography
                              variant="body2"
                              color="text.secondary"
                              sx={{
                                mt: 1,
                                fontFamily: '"Cascadia Code", "Consolas", monospace',
                                whiteSpace: "pre-wrap",
                                lineHeight: 1.7,
                              }}
                            >
                              {JSON.stringify(event.payload, null, 2)}
                            </Typography>
                            <Divider sx={{ mt: 2.5, borderColor: "rgba(145, 167, 196, 0.12)" }} />
                          </Box>
                        ))
                      )}
                    </Stack>
                  </CardContent>
                </Card>
              </Box>
            </Box>
          </Stack>
        </Container>
      </Box>
    </ThemeProvider>
  );
}
