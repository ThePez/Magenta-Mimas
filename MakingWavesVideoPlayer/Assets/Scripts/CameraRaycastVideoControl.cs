using UnityEngine;
using UnityEngine.UI;
using UnityEngine.Video;

public class CameraRaycastVideoControl : MonoBehaviour
{
    [Header("Configuration")] 
    public float raycastDistance = 30f;
    public float zoomSpeed = 1f;
    public float targetFOV = 15f;
    public float normalFOV = 60f;
    public float rotationSpeed = 5f;

    // -------------------------------------------------
    // Private state
    // -------------------------------------------------
    private float originalPitch; // Camera pitch at start
    private VideoPlayer lastVideoPlayer;
    private VideoPlayer[] videoPlayers;
    
    private PlayerRotateWithInertia playerRotation;
    private Camera mainCamera;
    private RawImage crosshair;

    void Start()
    {
        mainCamera = gameObject.GetComponent<Camera>();
        playerRotation = GameObject.FindGameObjectWithTag("Player").GetComponent<PlayerRotateWithInertia>();
        crosshair = GameObject.FindGameObjectWithTag("Crosshair").GetComponent<RawImage>();
        originalPitch = mainCamera.transform.eulerAngles.x;
        videoPlayers = FindObjectsByType<VideoPlayer>();
    }

    void Update()
    {
        Transform parent = transform.parent;
        Color crosshairColor = crosshair.color;
        
        foreach (VideoPlayer vp in videoPlayers)
        {
            Vector3 rayOrigin = parent.position;
            rayOrigin.y = vp.transform.position.y;
            Ray ray = new(rayOrigin, parent.forward);
            
            Debug.DrawRay(rayOrigin, rayOrigin + parent.forward * raycastDistance);

            if (!Physics.Raycast(ray, out RaycastHit hitInfo, raycastDistance))
            {
                continue;
            }

            // idk why tf vp isn't the same
            VideoPlayer videoPlayer = hitInfo.collider.GetComponent<VideoPlayer>();
            
            if (lastVideoPlayer != videoPlayer && playerRotation.Velocity == 0)
            {
                if (!videoPlayer.isPlaying)
                {
                    videoPlayer.Play();
                }

                lastVideoPlayer = videoPlayer;
            }
                
            if (playerRotation.Velocity == 0)
            {
                ZoomCamera(targetFOV);

                Quaternion current = mainCamera.transform.rotation;
                current.SetLookRotation(hitInfo.transform.position - mainCamera.transform.position);

                mainCamera.transform.rotation = Quaternion.Slerp(mainCamera.transform.rotation.normalized,
                    current.normalized, rotationSpeed * Time.deltaTime);
                
                crosshairColor.a = Mathf.Lerp(crosshairColor.a, 0, Time.deltaTime);
            }
            else
            {
                ZoomCamera(normalFOV);
                RotateCamera(originalPitch);
                
                crosshairColor.a = Mathf.Lerp(crosshairColor.a, 1, Time.deltaTime);
            }

            crosshair.color = crosshairColor;
            return;
        }
        
        ZoomCamera(normalFOV);
        RotateCamera(originalPitch);
        
        crosshairColor.a = Mathf.Lerp(crosshairColor.a, 1, Time.deltaTime);
        crosshair.color = crosshairColor;
            
        foreach (VideoPlayer vp in videoPlayers)
        {
            if (vp.isPlaying)
            {
                vp.Pause();
            }
        }
        
        lastVideoPlayer = null;
    }

    // -------------------------------------------------
    // Zoom and rotation helpers
    // -------------------------------------------------
    private void ZoomCamera(float newZoom)
    {
        mainCamera.fieldOfView = Mathf.Lerp(mainCamera.fieldOfView, newZoom, zoomSpeed * Time.deltaTime);
    }

    private void RotateCamera(float newPitch)
    {
        Quaternion targetRot = Quaternion.Euler(newPitch, mainCamera.transform.eulerAngles.y,
            mainCamera.transform.eulerAngles.z).normalized;
        mainCamera.transform.rotation = Quaternion.RotateTowards(mainCamera.transform.rotation.normalized, targetRot, 
            rotationSpeed * Time.deltaTime);
    }
}