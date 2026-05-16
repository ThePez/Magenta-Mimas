using UnityEngine;
using UnityEngine.Video;
using System.IO;

public class VideoSphereSpawnerFixedScale : MonoBehaviour
{
    [Header("Prefabs & Camera")]
    public GameObject spherePrefab;
    public Camera mainCamera;

    [Header("Layout")]
    public float padding = 1f;               // space between spheres
    public float planeHeight = 2f;           // height of the quad above each sphere
    public float fixedPlaneHeight = 1.5f;    // world‑units for the quad’s height
    public float minViewAngle = 15f;         // degrees – quad must stay outside this cone
    public float rotationStep = 5f;          // degrees to rotate parent each attempt

    private GameObject[] spheres;
    private VideoPlayer[] videoPlayers;
    private GameObject[] videoPlanes;

    void Start()
    {
        if (mainCamera == null) mainCamera = Camera.main;

        string path = Application.streamingAssetsPath;
        if (!Directory.Exists(path))
        {
            Debug.LogWarning("StreamingAssets folder does not exist!");
            return;
        }

        string[] mp4Files = Directory.GetFiles(path, "*.mp4");
        int n = mp4Files.Length;
        if (n == 0)
        {
            Debug.Log("No .mp4 files found.");
            return;
        }

        spheres      = new GameObject[n];
        videoPlayers = new VideoPlayer[n];
        videoPlanes  = new GameObject[n];

        // radius that gives roughly `padding` units of space between spheres
        float circumference = n * padding;
        float radius = circumference / (2f * Mathf.PI);

        // -------------------------------------------------------------
        // 1️⃣ Spawn all spheres + video quads (no view‑cone check yet)
        // -------------------------------------------------------------
        for (int i = 0; i < n; i++)
        {
            float angle = i * Mathf.PI * 2f / n;
            Vector3 spherePos = new Vector3(Mathf.Cos(angle) * radius, 0f, Mathf.Sin(angle) * radius);

            GameObject sphere = Instantiate(spherePrefab,
                                            transform.position + spherePos,
                                            Quaternion.identity,
                                            transform);
            spheres[i] = sphere;

            GameObject plane = CreateVideoQuad(sphere, mp4Files[i]);
            videoPlanes[i]  = plane;
            videoPlayers[i] = plane.GetComponent<VideoPlayer>();
        }

        // -------------------------------------------------------------
        // 2️⃣ Rotate the whole parent until no quad is inside the cone
        // -------------------------------------------------------------
        EnsureNoQuadInFront();
    }

    // -----------------------------------------------------------------
    // Rotate parent until every plane is outside the camera’s view cone
    // -----------------------------------------------------------------
    void EnsureNoQuadInFront()
    {
        int safetyIterations = 0;
        const int maxIterations = 360; // prevents infinite loops

        while (AnyPlaneInFront() && safetyIterations < maxIterations)
        {
            transform.Rotate(Vector3.up, rotationStep, Space.World);
            safetyIterations++;
        }

        if (safetyIterations == maxIterations)
            Debug.LogWarning("Could not find a rotation that clears the view cone.");
    }

    bool AnyPlaneInFront()
    {
        foreach (GameObject plane in videoPlanes)
        {
            if (plane == null) continue;

            Vector3 worldPos = plane.transform.position;
            Vector3 toPlane   = worldPos - mainCamera.transform.position;
            float angle = Vector3.Angle(mainCamera.transform.forward, toPlane);
            if (angle < minViewAngle) return true;
        }
        return false;
    }

    // -----------------------------------------------------------------
    // Helper: creates the quad, video player, render texture, etc.
    // -----------------------------------------------------------------
    GameObject CreateVideoQuad(GameObject parentSphere, string videoPath)
    {
        GameObject plane = GameObject.CreatePrimitive(PrimitiveType.Quad);
        plane.transform.parent = parentSphere.transform;
        plane.transform.localPosition = Vector3.up * planeHeight;

        // optional script you may have
        plane.AddComponent<ResetVideo>();

        // colliders & rigidbody
        Destroy(plane.GetComponent<Collider>());
        BoxCollider box = plane.AddComponent<BoxCollider>();
        box.isTrigger = true;

        Rigidbody rb = plane.AddComponent<Rigidbody>();
        rb.isKinematic = true;

        // VideoPlayer
        VideoPlayer vp = plane.AddComponent<VideoPlayer>();
        vp.playOnAwake = false;
        vp.source = VideoSource.Url;
        vp.url = Path.Combine(Application.streamingAssetsPath,
                              Path.GetFileName(videoPath));
        vp.isLooping = true;
        vp.waitForFirstFrame = true;

        // Audio
        AudioSource audio = plane.AddComponent<AudioSource>();
        vp.audioOutputMode = VideoAudioOutputMode.AudioSource;
        vp.SetTargetAudioSource(0, audio);

        // RenderTexture & material
        RenderTexture rt = new RenderTexture(1920, 1080, 0);
        rt.Create();

        Material videoMat = new Material(Shader.Find("Unlit/Texture"));
        videoMat.mainTexture = rt;
        plane.GetComponent<Renderer>().material = videoMat;
        vp.targetTexture = rt;

        // Adjust size when video is prepared
        vp.prepareCompleted += source =>
        {
            float aspect = (float)source.width / source.height;
            plane.transform.localScale = new Vector3(fixedPlaneHeight * aspect,
                                                    fixedPlaneHeight,
                                                    1f);

            // Cast uint → int (fixes CS1503)
            int texW = (int)source.width;
            int texH = (int)source.height;

            RenderTexture rtAdj = new RenderTexture(texW, texH, 0);
            rtAdj.Create();

            videoMat.mainTexture = rtAdj;
            source.targetTexture = rtAdj;

            source.frameReady += (s, frameIdx) =>
            {
                plane.GetComponent<Renderer>().material.mainTexture = s.targetTexture;
                s.Pause(); // keep paused after first frame
            };
            source.Pause();
        };

        vp.Prepare(); // async preparation, does not start playback
        return plane;
    }

    // -----------------------------------------------------------------
    // Update – make quads face the camera and handle clicks
    // -----------------------------------------------------------------
    void Update()
    {
        // Make every quad look at the camera
        for (int i = 0; i < spheres.Length; i++)
        {
            if (spheres[i] == null) continue;
            GameObject plane = videoPlanes[i];
            plane.transform.LookAt(mainCamera.transform);
            plane.transform.Rotate(0f, 180f, 0f);
        }

        // Raycast click handling
        if (Input.GetMouseButtonDown(0))
        {
            Ray ray = mainCamera.ScreenPointToRay(Input.mousePosition);
            if (Physics.Raycast(ray, out RaycastHit hit))
            {
                for (int i = 0; i < videoPlanes.Length; i++)
                {
                    if (hit.collider.gameObject == videoPlanes[i])
                    {
                        ToggleVideo(i);
                        break;
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------
    // Public control helpers
    // -----------------------------------------------------------------
    public void ToggleVideo(int index)
    {
        if (index < 0 || index >= videoPlayers.Length) return;
        VideoPlayer vp = videoPlayers[index];
        if (vp.isPlaying) vp.Pause(); else vp.Play();
    }

    public void PauseAllVideos()
    {
        foreach (var vp in videoPlayers)
            if (vp != null && vp.isPlaying) vp.Pause();
    }

    public void PlayVideo(int index, bool pauseOthers = true)
    {
        if (index < 0 || index >= videoPlayers.Length) return;
        if (pauseOthers) PauseAllVideos();
        VideoPlayer vp = videoPlayers[index];
        if (!vp.isPlaying) vp.Play();
    }
}
